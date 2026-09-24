#pragma once

#include "config.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace kos {

inline constexpr const char* WIKI_DATASET_NAME = "Wikipedia";
inline constexpr const char* WIKI_DATASET_NAME_LEGACY = "Wikipedia India";
inline constexpr const char* WIKI_SOURCE = "wikipedia.org";
inline constexpr int WIKI_GENERAL_EXTRA_PAGES = 100;
inline constexpr const char* SHARED_TOPIC_INDIA = "India";
inline constexpr const char* SHARED_TOPIC_GEO_INDIA = "Geography of India";
inline constexpr const char* SHARED_TOPIC_USA = "United States";
inline constexpr const char* SHARED_TOPIC_GERMANY = "Germany";
inline constexpr const char* SHARED_TOPIC_AUSTRALIA = "Australia";
inline constexpr int WIKI_MIN_SHARED_PAGES = 500;
inline constexpr int WIKI_MIN_COUNTRY_PAGES = 500;

struct WikiPage {
    std::string url;
    std::string title;
    std::string text;
    std::string topic;
    std::string published_at;
    std::string created_at;
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

WikiCrawlStats crawl_wikipedia(const Config& cfg, std::unordered_set<std::string>& already,
                               const std::function<void(const WikiPage&)>& on_page,
                               std::map<std::string, int> topic_counts = {});
inline WikiCrawlStats crawl_wikipedia_india(const Config& cfg, std::unordered_set<std::string>& already,
                                            const std::function<void(const WikiPage&)>& on_page) {
    return crawl_wikipedia(cfg, already, on_page);
}

std::map<std::string, std::string> fetch_last_revisions(const std::vector<std::string>& titles, const Config& cfg);
std::map<std::string, std::string> fetch_first_revisions(const std::vector<std::string>& titles, const Config& cfg);
std::map<std::string, std::string> parse_revision_query(const std::string& json);
std::string canonical_wiki_topic(const std::string& topic, const std::string& title = "");
inline std::string canonical_india_topic(const std::string& topic, const std::string& title = "") {
    return canonical_wiki_topic(topic, title);
}
bool is_shared_india_topic(const std::string& topic);
bool is_shared_country_topic(const std::string& topic);
std::string classify_shared_india_topic(const std::string& title, const std::string& text);
std::string classify_country_topic(const std::string& title, const std::string& text);
bool is_wikipedia_name(const std::string& name);

struct WikiExtract {
    std::string title;
    std::string text;
};

inline constexpr int WIKI_SHORT_EXTRACT_WORDS = 80;
inline constexpr int WIKI_MIN_ARTICLE_WORDS = 60;

std::map<std::string, std::string> parse_extract_query(const std::string& json);
std::optional<WikiExtract> fetch_wiki_extract(const std::string& title, const Config& cfg);

}  // namespace kos
