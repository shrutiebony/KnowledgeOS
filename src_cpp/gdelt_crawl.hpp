#pragma once

#include "config.hpp"

#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace kos {

inline constexpr const char* GDELT_DATASET_NAME = "GDELT";
inline constexpr const char* GDELT_SOURCE = "gdeltproject.org";

struct GdeltPage {
    std::string url;
    std::string title;
    std::string text;
    std::string topic;
    std::string published_at;
};

struct GdeltCrawlStats {
    int stored = 0;
    int failed = 0;
    int fetched = 0;
    std::string error;
};

bool is_gdelt_project_url(const std::string& url);

GdeltCrawlStats crawl_gdelt(const Config& cfg, std::unordered_set<std::string>& already,
                            const std::function<void(const GdeltPage&)>& on_page,
                            std::map<std::string, int> topic_counts = {});

}  // namespace kos
