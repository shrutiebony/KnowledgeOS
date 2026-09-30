#pragma once

#include "db.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include <map>
#include <string>

namespace kos {

inline int topic_count_of(const std::map<std::string, int>& counts, const std::string& topic) {
    auto it = counts.find(topic);
    return it == counts.end() ? 0 : it->second;
}

inline std::map<std::string, int> stored_topic_counts(Store& store, int64_t dataset_id) {
    std::map<std::string, int> out;
    for (const auto& d : store.docs_by_dataset(dataset_id, false)) {
        std::string raw = trim(d.topic);
        if (raw.empty()) continue;
        std::string key = canonical_wiki_topic(raw, raw);
        if (key.empty() || iequals(key, "General")) key = raw;
        out[key]++;
    }
    return out;
}

}  // namespace kos
