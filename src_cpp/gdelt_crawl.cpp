#include "gdelt_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iostream>
#include <ostream>
#include <map>
#include <thread>
#include <unordered_set>
#include <vector>

namespace kos {

static const char* UA =
    "KnowledgeOS-GDELT/1.0 (local educational corpus ingest; gdeltproject.org same-site)";

static int count_words(const std::string& text) {
    int n = 0;
    bool in = false;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '\'') {
            if (!in) {
                ++n;
                in = true;
            }
        } else {
            in = false;
        }
    }
    return n;
}

bool is_gdelt_project_url(const std::string& url) {
    auto h = comparable_host(url);
    if (!h) return false;
    const std::string root = "gdeltproject.org";
    if (*h == root) return true;
    if (h->size() > root.size() + 1 &&
        h->compare(h->size() - root.size(), root.size(), root) == 0 &&
        (*h)[h->size() - root.size() - 1] == '.') {
        return true;
    }
    return false;
}

static std::optional<std::string> canonicalize_gdelt(const std::string& raw) {
    auto n = normalize_url(raw);
    if (!n || !is_gdelt_project_url(*n) || skippable_url(*n)) return std::nullopt;
    return n;
}

static std::vector<std::string> extract_gdelt_links(const std::string& page_url, const std::string& html) {
    auto base = canonicalize_gdelt(page_url);
    if (!base) return {};
    auto parsed = parse_html(html);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& href : parsed.hrefs) {
        auto resolved = resolve_url(*base, href);
        if (!resolved) continue;
        auto can = canonicalize_gdelt(*resolved);
        if (!can || *can == *base) continue;
        if (seen.insert(*can).second) out.push_back(*can);
    }
    return out;
}

static std::string gdelt_date_iso(const std::string& seendate) {
    std::string d;
    for (char c : seendate) {
        if (std::isdigit(static_cast<unsigned char>(c))) d.push_back(c);
    }
    if (d.size() >= 8) return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
    return "";
}

static std::string ymdhms(int y, int m, int d, int h, int mi, int s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d", y, m, d, h, mi, s);
    return buf;
}

static void add_days(int& y, int& m, int& d, int delta) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d + delta;
    tm.tm_hour = 12;
    tm.tm_isdst = 0;
    std::mktime(&tm);
    y = tm.tm_year + 1900;
    m = tm.tm_mon + 1;
    d = tm.tm_mday;
}

static std::string metadata_text(const std::string& title, const std::string& url, const std::string& seendate,
                                 const std::string& domain, const std::string& country, const std::string& language,
                                 const std::string& topic) {
    std::string date = gdelt_date_iso(seendate);
    std::string out = "GDELT Project monitored news item titled \"" + title + "\".";
    if (!date.empty()) out += " Seen on " + date + ".";
    if (!domain.empty()) out += " Published by " + domain + ".";
    if (!country.empty()) out += " Source country " + country + ".";
    if (!language.empty()) out += " Language " + language + ".";
    out += " Topic label " + topic +
           ". Collected from the GDELT Project document API on api.gdeltproject.org. "
           "The GDELT Project monitors worldwide news coverage. ";
    if (!url.empty()) out += "Original article URL: " + url + ".";
    return out;
}

static int topic_count_of(const std::map<std::string, int>& counts, const std::string& topic) {
    auto it = counts.find(topic);
    return it == counts.end() ? 0 : it->second;
}

static bool store_gdelt_page(GdeltCrawlStats& stats, std::unordered_set<std::string>& already,
                             std::unordered_set<std::string>& stored_keys, std::map<std::string, int>& topic_counts,
                             int per_topic, const std::string& url, const std::string& title, const std::string& text,
                             const std::string& topic, const std::string& published_at,
                             const std::function<void(const GdeltPage&)>& on_page, std::string& last_error) {
    auto key = dedup_key(url);
    if (key && stored_keys.count(*key)) return false;
    if (count_words(text) < 40) return false;
    std::string assigned = classify_country_topic(title, text);
    if (assigned.empty()) assigned = topic;
    if (!is_shared_country_topic(assigned)) {
        if (is_shared_country_topic(topic)) assigned = topic;
        else return false;
    }
    if (topic_count_of(topic_counts, assigned) >= per_topic) return false;
    GdeltPage gp;
    gp.url = url;
    gp.title = title.empty() ? url : title;
    gp.text = text;
    gp.topic = assigned;
    gp.published_at = published_at;
    try {
        on_page(gp);
        stats.stored++;
        topic_counts[assigned]++;
        if (key) stored_keys.insert(*key);
        already.insert(url);
        if (stats.stored % 10 == 0 || topic_counts[assigned] % 25 == 0) {
            std::cerr << "GDELT stored " << stats.stored << " [" << assigned << " " << topic_counts[assigned] << "/"
                      << per_topic << "] (" << gp.title << ")\n";
        }
        return true;
    } catch (const std::exception& e) {
        stats.failed++;
        last_error = e.what();
        std::cerr << "GDELT store failed: " << last_error << "\n";
        return false;
    }
}

static HttpResponse gdelt_doc_get(const std::string& api, const Config& cfg) {
    int backoff = std::max(20000, cfg.gdelt_delay_ms * 4);
    HttpResponse res;
    for (int attempt = 0; attempt < 6; ++attempt) {
        res = http_get(api, UA, cfg.gdelt_timeout_ms, 2'000'000);
        if (res.status != 429 && res.status != 503 && res.status < 400 && !res.body.empty() && res.error.empty()) {
            return res;
        }
        std::cerr << "GDELT DOC API " << (res.status ? std::to_string(res.status) : res.error)
                  << ", retry in " << backoff << "ms\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(backoff * 2, 120000);
    }
    return res;
}

static void store_gdelt_articles(const nlohmann::json& root, const std::string& campaign_topic,
                                 GdeltCrawlStats& stats, std::unordered_set<std::string>& already,
                                 std::unordered_set<std::string>& stored_keys, std::map<std::string, int>& topic_counts,
                                 int per_topic, const std::function<void(const GdeltPage&)>& on_page,
                                 std::string& last_error) {
    if (!root.contains("articles") || !root["articles"].is_array()) return;
    for (const auto& art : root["articles"]) {
        if (topic_count_of(topic_counts, campaign_topic) >= per_topic) return;
        std::string url = art.value("url", "");
        std::string title = trim(art.value("title", ""));
        if (url.empty()) continue;
        auto canon = normalize_url(url);
        std::string stored_url = canon.value_or(url);
        auto key = dedup_key(stored_url);
        if (key && stored_keys.count(*key)) continue;
        std::string seendate = art.value("seendate", "");
        std::string domain = art.value("domain", "");
        std::string country = art.value("sourcecountry", "");
        std::string language = art.value("language", "");
        std::string topic = campaign_topic;
        std::string text = metadata_text(title.empty() ? stored_url : title, stored_url, seendate, domain, country,
                                         language, topic);
        store_gdelt_page(stats, already, stored_keys, topic_counts, per_topic, stored_url, title, text, topic,
                         gdelt_date_iso(seendate), on_page, last_error);
    }
}

static void harvest_gdelt_country(const Config& cfg, const char* topic, const std::vector<const char*>& queries,
                                  GdeltCrawlStats& stats, std::unordered_set<std::string>& already,
                                  std::unordered_set<std::string>& stored_keys, std::map<std::string, int>& topic_counts,
                                  int per_topic, const std::function<void(const GdeltPage&)>& on_page,
                                  std::string& last_error) {
    static const char* spans[] = {"1w", "1m", "3m"};
    if (topic_count_of(topic_counts, topic) >= per_topic) return;
    std::cerr << "Harvesting GDELT DOC API news for " << topic << " (have "
              << topic_count_of(topic_counts, topic) << ", want " << per_topic << ").\n" << std::flush;

    for (const char* span : spans) {
        for (const char* query : queries) {
            if (topic_count_of(topic_counts, topic) >= per_topic) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg.gdelt_delay_ms, 2500)));
            std::string api = "https://api.gdeltproject.org/api/v2/doc/doc?mode=ArtList&format=json&maxrecords=250&sort=DateDesc&timespan=" +
                              std::string(span) + "&query=" + url_encode(query);
            stats.fetched++;
            auto res = gdelt_doc_get(api, cfg);
            if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
                stats.failed++;
                last_error = res.error.empty() ? ("DOC API HTTP " + std::to_string(res.status)) : res.error;
                continue;
            }
            nlohmann::json root;
            try {
                root = nlohmann::json::parse(res.body);
            } catch (...) {
                stats.failed++;
                last_error = "DOC API returned non-JSON";
                continue;
            }
            store_gdelt_articles(root, topic, stats, already, stored_keys, topic_counts, per_topic, on_page,
                                 last_error);
        }
    }

    int y = 2026, m = 9, d = 23;
    const int window_days = 3;
    for (int w = 0; w < 80 && topic_count_of(topic_counts, topic) < per_topic; ++w) {
        int ey = y, em = m, ed = d;
        add_days(y, m, d, -window_days);
        std::string end = ymdhms(ey, em, ed, 23, 59, 59);
        std::string start = ymdhms(y, m, d, 0, 0, 0);
        for (const char* query : queries) {
            if (topic_count_of(topic_counts, topic) >= per_topic) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg.gdelt_delay_ms, 2500)));
            std::string api = "https://api.gdeltproject.org/api/v2/doc/doc?mode=ArtList&format=json&maxrecords=250&sort=DateDesc&query=" +
                              url_encode(query) + "&startdatetime=" + start + "&enddatetime=" + end;
            stats.fetched++;
            auto res = gdelt_doc_get(api, cfg);
            if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
                stats.failed++;
                last_error = res.error.empty() ? ("DOC API HTTP " + std::to_string(res.status)) : res.error;
                continue;
            }
            nlohmann::json root;
            try {
                root = nlohmann::json::parse(res.body);
            } catch (...) {
                continue;
            }
            store_gdelt_articles(root, topic, stats, already, stored_keys, topic_counts, per_topic, on_page,
                                 last_error);
        }
    }
}

static bool country_topics_need_pages(const std::map<std::string, int>& topic_counts, int per_topic) {
    return topic_count_of(topic_counts, SHARED_TOPIC_USA) < per_topic ||
           topic_count_of(topic_counts, SHARED_TOPIC_GERMANY) < per_topic ||
           topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) < per_topic;
}

GdeltCrawlStats crawl_gdelt(const Config& cfg, std::unordered_set<std::string>& already,
                            const std::function<void(const GdeltPage&)>& on_page,
                            std::map<std::string, int> topic_counts) {
    static const char* seeds[] = {
        "https://www.gdeltproject.org/",
        "https://www.gdeltproject.org/data.html",
        "https://blog.gdeltproject.org/",
        "https://blog.gdeltproject.org/?s=united+states",
        "https://blog.gdeltproject.org/?s=germany",
        "https://blog.gdeltproject.org/?s=australia",
        "https://blog.gdeltproject.org/category/documentation/",
        "https://blog.gdeltproject.org/category/announcements/"
    };
    struct Item {
        std::string url;
        int depth;
        bool seed;
    };
    std::deque<Item> queue;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> stored_keys;
    for (const auto& url : already) {
        auto key = dedup_key(canonicalize_gdelt(url).value_or(url));
        if (key) stored_keys.insert(*key);
    }
    for (const char* seed : seeds) {
        auto n = canonicalize_gdelt(seed);
        auto key = n ? dedup_key(*n) : std::nullopt;
        if (!key) continue;
        seen.insert(*key);
        queue.push_back({*n, 0, true});
    }

    GdeltCrawlStats stats;
    stats.stored = static_cast<int>(stored_keys.size());
    std::string last_error;
    const int per_topic = std::max(cfg.gdelt_max_pages, WIKI_MIN_COUNTRY_PAGES);

    if (country_topics_need_pages(topic_counts, per_topic)) {
        harvest_gdelt_country(cfg, SHARED_TOPIC_USA,
                              {"sourcecountry:US sourcelang:english", "\"united states\" sourcelang:english"},
                              stats, already, stored_keys, topic_counts, per_topic, on_page, last_error);
        harvest_gdelt_country(cfg, SHARED_TOPIC_GERMANY,
                              {"sourcecountry:GM sourcelang:english", "germany sourcelang:english"},
                              stats, already, stored_keys, topic_counts, per_topic, on_page, last_error);
        harvest_gdelt_country(cfg, SHARED_TOPIC_AUSTRALIA,
                              {"sourcecountry:AS sourcelang:english", "australia sourcelang:english"},
                              stats, already, stored_keys, topic_counts, per_topic, on_page, last_error);
    }

    const int max_site_fetches = 40;
    int site_fetches = 0;
    while (!queue.empty() && country_topics_need_pages(topic_counts, per_topic) && site_fetches < max_site_fetches) {
        Item item = queue.front();
        queue.pop_front();
        if (stats.fetched > 0 && cfg.gdelt_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.gdelt_delay_ms));
        }
        stats.fetched++;
        site_fetches++;
        auto page = http_get(item.url, UA, cfg.gdelt_timeout_ms, 2'000'000);
        if (page.status == 429 || page.status == 503) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg.gdelt_delay_ms, 400)));
            page = http_get(item.url, UA, cfg.gdelt_timeout_ms, 2'000'000);
        }
        if (page.status >= 400 || page.body.empty() || !page.error.empty()) {
            stats.failed++;
            last_error = page.error.empty() ? ("HTTP " + std::to_string(page.status)) : page.error;
            continue;
        }
        std::string ctype = ascii_lower(page.content_type);
        if (!ctype.empty() && ctype.find("html") == std::string::npos && ctype.find("xml") == std::string::npos &&
            ctype.find("text/") == std::string::npos) {
            stats.failed++;
            continue;
        }
        std::string stored_url = page.final_url.empty() ? item.url : canonicalize_gdelt(page.final_url).value_or(item.url);
        if (!is_gdelt_project_url(stored_url)) {
            stats.failed++;
            continue;
        }
        auto stored_key = dedup_key(stored_url);
        if (stored_key) seen.insert(*stored_key);

        auto html = parse_html(page.body);
        std::string title = trim(html.title);
        if (title.empty()) title = stored_url;
        std::string text = html.body_text;
        bool enough = count_words(text) >= 40;
        if (enough) {
            bool already_have = stored_key && stored_keys.count(*stored_key);
            if (!already_have) {
                std::string topic = classify_country_topic(title, text);
                if (is_shared_country_topic(topic)) {
                    std::string published;
                    if (auto d = nonempty(html.last_modified_meta)) published = *d;
                    else if (auto d = nonempty(html.time_datetime)) published = *d;
                    store_gdelt_page(stats, already, stored_keys, topic_counts, per_topic, stored_url, title, text,
                                     topic, published, on_page, last_error);
                }
            }
        } else if (item.seed) {
            last_error = "Seed page had no extractable text: " + stored_url;
        }
        if (item.depth >= cfg.gdelt_max_depth) continue;
        for (const auto& link : extract_gdelt_links(stored_url, page.body)) {
            auto key = dedup_key(link);
            if (!key || !seen.insert(*key).second) continue;
            queue.push_back({link, item.depth + 1, false});
        }
    }

    if (stats.stored == 0 && already.empty()) {
        stats.error = "Could not reach GDELT (gdeltproject.org) from the documented project/blog seeds. Last error: " +
                      (last_error.empty() ? std::string("no page text could be extracted") : last_error);
    }
    return stats;
}

}  // namespace kos
