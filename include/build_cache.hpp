#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace ssg {

struct CacheEntry {
    int64_t mtime;
    std::string hash;
};

// Persists per-page build state between runs, plus one global template hash.
//
// Two-tier check per page:
//   1. mtime fast path: if the file's OS mtime matches what we saw last
//      time (and the template hasn't changed since), trust it's unchanged
//      with no read/hash needed at all.
//   2. content hash fallback: if mtime differs (or this is the first run),
//      read + sha256 the file. This also catches the case where mtime was
//      touched without content changing (e.g. a git checkout) -- the hash
//      matching means we skip the render but still refresh the mtime.
//
// A page's combined hash = sha256(markdown source + template source), so
// touching either the page or the template it depends on invalidates the
// entry -- a lightweight stand-in for a full dependency graph.
class BuildCache {
public:
    void load(const std::string& cacheFilePath);
    void save(const std::string& cacheFilePath) const;

    const CacheEntry* get(const std::string& key) const;
    void update(const std::string& key, int64_t mtime, const std::string& hash);

    std::string templateHash() const { return templateHash_; }
    void setTemplateHash(const std::string& h) { templateHash_ = h; }

private:
    std::unordered_map<std::string, CacheEntry> entries_;
    std::string templateHash_;
};

} // namespace ssg
