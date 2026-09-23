#pragma once

#include "config.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace kos {

inline constexpr const char* WIKI_DATASET_NAME = "Wikipedia India";
inline constexpr const char* WIKI_SOURCE = "wikipedia.org";

struct WikiPage {
    std::string url;
    std::string title;
    std::string text;
    std::string topic;
    std::string published_at;
};

struct WikiCrawlStats {
    int stored = 0;
    int failed = 0;
    int fetched = 0;
    std::string error;
};

bool is_en_wikipedia(const std::string& url);
std::optional<std::string> canonicalize_wiki_url(const std::string& raw);
bool wiki_should_follow(const std::string& url);
bool is_main_namespace_article(const std::string& url);
std::optional<std::string> wiki_title_from_url(const std::string& url);
std::string article_title_from_raw(const std::string& raw);

WikiCrawlStats crawl_wikipedia_india(const Config& cfg, std::unordered_set<std::string>& already,
                                     const std::function<void(const WikiPage&)>& on_page);

std::map<std::string, std::string> fetch_last_revisions(const std::vector<std::string>& titles, const Config& cfg);
std::map<std::string, std::string> parse_revision_query(const std::string& json);

}  // namespace kos
