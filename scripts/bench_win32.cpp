#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <omp.h>

using Clock = std::chrono::steady_clock;

struct FileItem {
    std::string relPath;
    int64_t mtime;
};

void scanDir(const std::wstring& root, const std::string& relPrefix, std::vector<FileItem>& files) {
    std::wstring search = root + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(search.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') {
            if (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)) continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subRoot = root + L"\\" + fd.cFileName;
            // Convert to utf8
            char utf8[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8, sizeof(utf8), NULL, NULL);
            std::string subPrefix = relPrefix.empty() ? utf8 : (relPrefix + "/" + utf8);
            scanDir(subRoot, subPrefix, files);
        } else {
            size_t len = wcslen(fd.cFileName);
            if (len >= 3 && _wcsicmp(fd.cFileName + len - 3, L".md") == 0) {
                char utf8[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8, sizeof(utf8), NULL, NULL);
                std::string rel = relPrefix.empty() ? utf8 : (relPrefix + "/" + utf8);
                ULARGE_INTEGER uli;
                uli.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                files.push_back({rel, static_cast<int64_t>(uli.QuadPart)});
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int main() {
    auto t0 = Clock::now();
    std::vector<FileItem> files;
    files.reserve(12000);
    scanDir(L"D:\\ssg (1)\\ssg\\content_loadtest", "", files);
    auto t1 = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Sequential Win32 FindFirstFileEx scanned " << files.size() << " files in " << ms << " ms\n";
    return 0;
}
