#pragma once
#include <string>

namespace ssg {

// Converts a markdown document to an HTML fragment (no <html>/<body> wrapper).
// Supports: # .. ###### headers, **bold**/__bold__, *italic*/_italic_,
// `inline code`, [text](url), - / * unordered lists, > blockquotes,
// ``` fenced code blocks, and paragraphs. Escapes raw HTML for safety.
std::string renderMarkdown(const std::string& markdown);

} // namespace ssg
