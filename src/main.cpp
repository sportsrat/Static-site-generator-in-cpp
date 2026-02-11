#include "markdown.hpp"
#include "sha256.hpp"
#include "build_cache.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct FileInfo {
    std::string relPath;      // e.g. "blog/vol0/post1.md"
    std::string fullPath;     // e.g. "content_loadtest/blog/vol0/post1.md"
    int64_t mtime;            // 64-bit FILETIME or epoch count
};

#ifdef _WIN32
static void scanDirectoryWin32(const std::wstring& rootW, const std::string& relPrefix, std::vector<FileInfo>& files) {
    std::wstring searchPattern = rootW + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(searchPattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') {
            if (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)) continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subRoot = rootW + L"\\" + fd.cFileName;
            char utf8[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8, sizeof(utf8), NULL, NULL);
            std::string subPrefix = relPrefix.empty() ? utf8 : (relPrefix + "/" + utf8);
            scanDirectoryWin32(subRoot, subPrefix, files);
        } else {
            size_t len = wcslen(fd.cFileName);
            if (len >= 3 && _wcsicmp(fd.cFileName + len - 3, L".md") == 0) {
                char utf8[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8, sizeof(utf8), NULL, NULL);
                std::string rel = relPrefix.empty() ? utf8 : (relPrefix + "/" + utf8);

                char rootUtf8[MAX_PATH * 2];
                WideCharToMultiByte(CP_UTF8, 0, rootW.c_str(), -1, rootUtf8, sizeof(rootUtf8), NULL, NULL);
                std::string full = std::string(rootUtf8) + "/" + utf8;
                for (char& c : full) if (c == '\\') c = '/';

                ULARGE_INTEGER uli;
                uli.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                files.push_back({std::move(rel), std::move(full), static_cast<int64_t>(uli.QuadPart)});
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}
#else
static void scanDirectoryPortable(const fs::path& contentDir, std::vector<FileInfo>& files) {
    std::string prefix = contentDir.generic_string();
    size_t prefixLen = prefix.size();
    if (!prefix.empty() && prefix.back() != '/') prefixLen++;

    for (const auto& entry : fs::recursive_directory_iterator(contentDir)) {
        if (!entry.is_regular_file()) continue;
        std::string pStr = entry.path().generic_string();
        if (pStr.size() >= 3 && pStr.substr(pStr.size() - 3) == ".md") {
            std::string rel = (pStr.size() > prefixLen) ? pStr.substr(prefixLen) : pStr;
            int64_t mtimeVal = entry.last_write_time().time_since_epoch().count();
            files.push_back({std::move(rel), pStr, mtimeVal});
        }
    }
}
#endif

static double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not read " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

int main(int argc, char* argv[]) {
    fs::path contentDir = "content";
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") verbose = true;
        else contentDir = fs::path(arg);
    }
    const fs::path templatePath = "templates/layout.html";
    const fs::path distDir = "dist";
    const fs::path cachePath = ".ssg-cache";

    std::cout << "========================================\n";
    std::cout << "BUILDING STATIC SITE...\n";
    std::cout << "Content dir: " << contentDir.generic_string() << "\n";
    std::cout << "========================================\n";

    if (!fs::exists(templatePath)) {
        std::cerr << "ERROR: Could not read " << templatePath << "\n";
        return 1;
    }
    std::string templateContent = readFile(templatePath);
    std::string templateHash = ssg::Sha256::hash(templateContent);

    auto contentMarker = templateContent.find("{{content}}");
    if (contentMarker == std::string::npos) {
        std::cerr << "ERROR: {{content}} placeholder not found in template.\n";
        return 1;
    }

    if (!fs::exists(contentDir)) {
        std::cerr << "ERROR: content/ directory not found.\n";
        return 1;
    }

    auto t0 = Clock::now();
    ssg::BuildCache cache;
    cache.load(cachePath.string());
    double cacheLoadMs = msSince(t0);

    bool templateUnchanged = !cache.templateHash().empty() && cache.templateHash() == templateHash;

    int built = 0, skipped = 0, failed = 0;
    int mtimeFastSkips = 0; // skipped without ever reading the file
    double totalBuiltMs = 0.0, slowestMs = 0.0;
    fs::path slowestPage;
    Clock::time_point buildStart = Clock::now();

    // Fast output verification: if distDir doesn't exist, we must rebuild everything.
    // If verifyDist is true, we scan dist/ to ensure every file is physically present.
    bool verifyDist = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--verify-dist") verifyDist = true;
    }
    bool distDirExists = fs::exists(distDir);

    auto tScanDist = Clock::now();
    std::unordered_set<std::string> existingOutputs;
    if (verifyDist && distDirExists) {
        for (const auto& e : fs::recursive_directory_iterator(distDir)) {
            if (e.is_regular_file()) {
                existingOutputs.insert(e.path().lexically_relative(distDir).generic_string());
            }
        }
    }
    double distScanMs = msSince(tScanDist);

    // Fast directory traversal using Win32 FindFirstFileExW (or portable fallback)
    auto tScan = Clock::now();
    std::vector<FileInfo> files;
    files.reserve(16384);
#ifdef _WIN32
    scanDirectoryWin32(contentDir.wstring(), "", files);
#else
    scanDirectoryPortable(contentDir, files);
#endif
    double dirScanMs = msSince(tScan);

    struct ThreadResult {
        int built = 0;
        int skipped = 0;
        int failed = 0;
        int mtimeFastSkips = 0;
        double totalBuiltMs = 0.0;
        double slowestMs = 0.0;
        std::string slowestPage;
        std::vector<std::pair<std::string, ssg::CacheEntry>> cacheUpdates;
    };

    int numThreads = 1;
#ifdef _OPENMP
    numThreads = omp_get_max_threads();
#endif
    std::vector<ThreadResult> threadResults(numThreads);

    auto tLoop = Clock::now();
#pragma omp parallel
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        auto& res = threadResults[tid];

#pragma omp for schedule(dynamic, 64)
        for (size_t i = 0; i < files.size(); i++) {
            const auto& file = files[i];
            Clock::time_point pageStart = Clock::now();

            std::string outRelKey = file.relPath;
            if (outRelKey.size() >= 3 && outRelKey.compare(outRelKey.size() - 3, 3, ".md") == 0) {
                outRelKey.replace(outRelKey.size() - 3, 3, ".html");
            }

            fs::path outPath = distDir / outRelKey;
            const std::string& cacheKey = file.relPath;
            int64_t mtimeVal = file.mtime;

            const auto* cached = cache.get(cacheKey);
            bool outExists = !verifyDist ? distDirExists : (existingOutputs.find(outRelKey) != existingOutputs.end());

            // Fast path: template unchanged, mtime matches what we saw last
            // time, and the previous output still exists -> trust it, no I/O.
            if (templateUnchanged && cached && cached->mtime == mtimeVal && outExists) {
                res.mtimeFastSkips++;
                res.skipped++;
                continue;
            }

            std::string mdContent;
            try {
                mdContent = readFile(file.fullPath);
            } catch (const std::exception& e) {
                #pragma omp critical
                std::cerr << "ERROR: " << e.what() << "\n";
                res.failed++;
                continue;
            }

            // Combined hash ties the page to both its own content and the
            // template -- either changing invalidates the cache entry.
            std::string combinedHash = ssg::Sha256::hash(mdContent + "\x1e" + templateHash);

            // mtime changed (e.g. a checkout touched it) but content didn't --
            // verify actual output file exists before skipping
            bool actualOutExists = fs::exists(outPath);
            if (cached && cached->hash == combinedHash && actualOutExists) {
                res.cacheUpdates.push_back({cacheKey, ssg::CacheEntry{mtimeVal, combinedHash}});
                res.skipped++;
                continue;
            }

            std::string htmlBody = ssg::renderMarkdown(mdContent);
            std::string page = templateContent.substr(0, contentMarker) + htmlBody +
                                templateContent.substr(contentMarker + std::string("{{content}}").size());

            try {
                writeFile(outPath, page);
            } catch (const std::exception& e) {
                #pragma omp critical
                std::cerr << "ERROR writing " << outPath << ": " << e.what() << "\n";
                res.failed++;
                continue;
            }

            res.cacheUpdates.push_back({cacheKey, ssg::CacheEntry{mtimeVal, combinedHash}});
            double pageMs = msSince(pageStart);
            res.totalBuiltMs += pageMs;
            if (pageMs > res.slowestMs) {
                res.slowestMs = pageMs;
                res.slowestPage = outPath.generic_string();
            }

            if (verbose) {
                #pragma omp critical
                std::cout << "BUILD (changed):  " << outPath.generic_string()
                           << "  [" << std::fixed << std::setprecision(3) << pageMs << " ms]\n";
            }
            res.built++;
        }
    }
    double loopMs = msSince(tLoop);

    bool cacheDirty = built > 0;
    for (const auto& tr : threadResults) {
        built += tr.built;
        skipped += tr.skipped;
        failed += tr.failed;
        mtimeFastSkips += tr.mtimeFastSkips;
        totalBuiltMs += tr.totalBuiltMs;
        if (tr.slowestMs > slowestMs) {
            slowestMs = tr.slowestMs;
            slowestPage = fs::path(tr.slowestPage);
        }
        if (!tr.cacheUpdates.empty()) cacheDirty = true;
        for (const auto& [k, v] : tr.cacheUpdates) {
            cache.update(k, v.mtime, v.hash);
        }
    }

    auto tSave = Clock::now();
    if (cacheDirty) {
        cache.setTemplateHash(templateHash);
        cache.save(cachePath.string());
    }
    double cacheSaveMs = msSince(tSave);

    double totalMs = msSince(buildStart);
    int totalFiles = built + skipped;
    double hitRate = totalFiles > 0 ? (100.0 * skipped / totalFiles) : 0.0;
    double avgBuiltMs = built > 0 ? (totalBuiltMs / built) : 0.0;
    double throughput = totalMs > 0 ? (totalFiles * 1000.0 / totalMs) : 0.0;

    std::cout << "========================================\n";
    std::cout << "BUILD COMPLETE - " << built << " built, " << skipped
               << " skipped, " << failed << " failed\n";
    std::cout << "  (" << mtimeFastSkips << "/" << skipped
               << " skips used the mtime fast path -- no file read/hash)\n";
    std::cout << "----------------------------------------\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total wall time:     " << totalMs << " ms\n";
    std::cout << "  - Cache load:      " << cacheLoadMs << " ms\n";
    std::cout << "  - Dir scan:        " << dirScanMs << " ms\n";
    std::cout << "  - Eval/Build loop: " << loopMs << " ms (" << numThreads << " threads)\n";
    std::cout << "  - Cache save:      " << cacheSaveMs << " ms\n";
    std::cout << "Cache hit rate:      " << std::setprecision(1) << hitRate << "%\n";
    std::cout << std::setprecision(3);
    if (built > 0) {
        std::cout << "Avg latency/page:    " << avgBuiltMs << " ms (rebuilt pages only)\n";
        std::cout << "Slowest page:        " << slowestPage.generic_string()
                   << " (" << slowestMs << " ms)\n";
    }
    std::cout << "Throughput:          " << std::setprecision(1) << throughput << " files/sec\n";
    std::cout << "========================================\n";

    // Append to a persistent metrics log so trends across runs are visible,
    // not just the current one. Simple CSV: easy to graph in a spreadsheet.
    {
        bool isNewLog = !fs::exists(".ssg-metrics.csv");
        std::ofstream log(".ssg-metrics.csv", std::ios::app);
        if (isNewLog) {
            log << "timestamp,total_ms,built,skipped,failed,cache_hit_pct,throughput_files_per_sec\n";
        }
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        log << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << ","
            << std::fixed << std::setprecision(3) << totalMs << ","
            << built << "," << skipped << "," << failed << ","
            << std::setprecision(1) << hitRate << "," << throughput << "\n";
    }

    return failed > 0 ? 1 : 0;
}