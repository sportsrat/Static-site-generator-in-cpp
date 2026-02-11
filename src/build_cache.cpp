#include "build_cache.hpp"
#include <fstream>
#include <sstream>

namespace ssg {

void BuildCache::load(const std::string& cacheFilePath) {
    entries_.clear();
    templateHash_.clear();
    std::ifstream in(cacheFilePath);
    if (!in) return; // no cache yet -- first run, everything builds

    std::string line;
    if (std::getline(in, line)) {
        // First line: "TEMPLATE\t<hash>"
        auto tab = line.find('\t');
        if (tab != std::string::npos && line.substr(0, tab) == "TEMPLATE") {
            templateHash_ = line.substr(tab + 1);
        }
    }
    while (std::getline(in, line)) {
        // "key\tmtime\thash"
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) continue;
        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) continue;
        std::string key = line.substr(0, tab1);
        int64_t mtime = std::stoll(line.substr(tab1 + 1, tab2 - tab1 - 1));
        std::string hash = line.substr(tab2 + 1);
        entries_[key] = CacheEntry{mtime, hash};
    }
}

void BuildCache::save(const std::string& cacheFilePath) const {
    std::ofstream out(cacheFilePath, std::ios::trunc);
    out << "TEMPLATE\t" << templateHash_ << '\n';
    for (const auto& [key, entry] : entries_) {
        out << key << '\t' << entry.mtime << '\t' << entry.hash << '\n';
    }
}

const CacheEntry* BuildCache::get(const std::string& key) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    return &it->second;
}

void BuildCache::update(const std::string& key, int64_t mtime, const std::string& hash) {
    entries_[key] = CacheEntry{mtime, hash};
}

} // namespace ssg
