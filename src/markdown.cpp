#include "markdown.hpp"
#include <sstream>
#include <vector>
#include <regex>

namespace ssg {

static std::string escapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

// Renders inline spans: `code`, [links](url), **bold**, *italic*.
// Code spans are protected with placeholders so bold/italic regexes
// never reach inside them.
static std::string renderInline(const std::string& rawText) {
    std::string text = escapeHtml(rawText);

    std::vector<std::string> codeSpans;
    {
        static const std::regex codeRe("`([^`]+)`");
        std::string result;
        size_t lastPos = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), codeRe);
             it != std::sregex_iterator(); ++it) {
            auto m = *it;
            result += text.substr(lastPos, m.position() - lastPos);
            codeSpans.push_back("<code>" + m[1].str() + "</code>");
            result += "\x01" + std::to_string(codeSpans.size() - 1) + "\x02";
            lastPos = static_cast<size_t>(m.position() + m.length());
        }
        result += text.substr(lastPos);
        text = result;
    }

    static const std::regex linkRe(R"(\[([^\]]+)\]\(([^)]+)\))");
    text = std::regex_replace(text, linkRe, "<a href=\"$2\">$1</a>");

    static const std::regex boldRe(R"(\*\*([^*]+)\*\*|__([^_]+)__)");
    text = std::regex_replace(text, boldRe, "<strong>$1$2</strong>");

    static const std::regex italicRe(R"(\*([^*]+)\*|_([^_]+)_)");
    text = std::regex_replace(text, italicRe, "<em>$1$2</em>");

    // Restore protected code spans
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\x01') {
            size_t j = text.find('\x02', i);
            int idx = std::stoi(text.substr(i + 1, j - i - 1));
            result += codeSpans[static_cast<size_t>(idx)];
            i = j + 1;
        } else {
            result += text[i];
            i++;
        }
    }
    return result;
}

std::string renderMarkdown(const std::string& markdown) {
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : markdown) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    std::ostringstream html;
    std::vector<std::string> paragraphBuffer;
    bool inList = false;
    bool inQuote = false;

    auto flushParagraph = [&]() {
        if (!paragraphBuffer.empty()) {
            std::string joined;
            for (size_t i = 0; i < paragraphBuffer.size(); i++) {
                if (i) joined += " ";
                joined += paragraphBuffer[i];
            }
            html << "<p>" << renderInline(joined) << "</p>\n";
            paragraphBuffer.clear();
        }
    };
    auto closeList = [&]() { if (inList) { html << "</ul>\n"; inList = false; } };
    auto closeQuote = [&]() { if (inQuote) { html << "</blockquote>\n"; inQuote = false; } };

    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& raw = lines[i];

        // Fenced code block
        if (raw.rfind("```", 0) == 0) {
            flushParagraph(); closeList(); closeQuote();
            html << "<pre><code>";
            i++;
            while (i < lines.size() && lines[i].rfind("```", 0) != 0) {
                html << escapeHtml(lines[i]) << "\n";
                i++;
            }
            html << "</code></pre>\n";
            continue;
        }

        bool isBlank = raw.find_first_not_of(" \t") == std::string::npos;
        if (isBlank) { flushParagraph(); closeList(); closeQuote(); continue; }

        // Header: 1-6 leading '#' followed by a space
        size_t hashCount = 0;
        while (hashCount < raw.size() && raw[hashCount] == '#') hashCount++;
        if (hashCount >= 1 && hashCount <= 6 && hashCount < raw.size() && raw[hashCount] == ' ') {
            flushParagraph(); closeList(); closeQuote();
            std::string content = raw.substr(hashCount + 1);
            html << "<h" << hashCount << ">" << renderInline(content) << "</h" << hashCount << ">\n";
            continue;
        }

        // Blockquote
        if (raw.rfind("> ", 0) == 0 || raw == ">") {
            flushParagraph(); closeList();
            if (!inQuote) { html << "<blockquote>\n"; inQuote = true; }
            std::string content = raw.size() > 2 ? raw.substr(2) : "";
            html << "<p>" << renderInline(content) << "</p>\n";
            continue;
        }
        closeQuote();

        // Unordered list ("- " or "* ")
        if (raw.rfind("- ", 0) == 0 || raw.rfind("* ", 0) == 0) {
            flushParagraph();
            if (!inList) { html << "<ul>\n"; inList = true; }
            html << "<li>" << renderInline(raw.substr(2)) << "</li>\n";
            continue;
        }
        closeList();

        paragraphBuffer.push_back(raw);
    }

    flushParagraph(); closeList(); closeQuote();
    return html.str();
}

} // namespace ssg
