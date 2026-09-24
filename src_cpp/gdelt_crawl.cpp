#include "gdelt_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include "json.hpp"
#include "miniz.h"

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

static bool country_topics_need_pages(const std::map<std::string, int>& topic_counts, int per_topic);

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

static bool g_doc_api_cool = false;

static HttpResponse gdelt_doc_get(const std::string& api, const Config& cfg) {
    HttpResponse res;
    if (g_doc_api_cool) {
        res.error = "DOC API cooling after 429";
        res.status = 429;
        return res;
    }
    int backoff = std::max(45000, cfg.gdelt_delay_ms * 8);
    for (int attempt = 0; attempt < 3; ++attempt) {
        res = http_get(api, UA, cfg.gdelt_timeout_ms, 2'000'000);
        if (res.status != 429 && res.status != 503 && res.status < 400 && !res.body.empty() && res.error.empty()) {
            return res;
        }
        std::cerr << "GDELT DOC API " << (res.status ? std::to_string(res.status) : res.error)
                  << ", retry in " << backoff << "ms\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(backoff * 2, 180000);
    }
    if (res.status == 429 || res.status == 503) {
        g_doc_api_cool = true;
        std::cerr << "GDELT DOC API still 429 — skipping remaining DOC queries, using GKG files instead.\n"
                  << std::flush;
    }
    return res;
}

static std::string unzip_first_file(const std::string& bytes) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return "";
    std::string out;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!p) continue;
        out.assign(static_cast<char*>(p), sz);
        mz_free(p);
        break;
    }
    mz_zip_reader_end(&zip);
    return out;
}

static std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(std::move(cur));
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    fields.push_back(std::move(cur));
    return fields;
}

static std::string prev_gkg_stamp(const std::string& stamp) {
    if (stamp.size() < 12) return "";
    int y = std::stoi(stamp.substr(0, 4));
    int m = std::stoi(stamp.substr(4, 2));
    int d = std::stoi(stamp.substr(6, 2));
    int h = std::stoi(stamp.substr(8, 2));
    int mi = std::stoi(stamp.substr(10, 2));
    mi -= 15;
    if (mi < 0) {
        mi += 60;
        h--;
    }
    if (h < 0) {
        h += 24;
        add_days(y, m, d, -1);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d00", y, m, d, h, mi);
    return buf;
}

static std::string latest_gkg_stamp(const Config& cfg) {
    static const char* urls[] = {
        "http://data.gdeltproject.org/gdeltv2/lastupdate.txt",
        "https://data.gdeltproject.org/gdeltv2/lastupdate.txt"};
    for (const char* u : urls) {
        auto res = http_get(u, UA, std::max(cfg.gdelt_timeout_ms, 20000), 200000);
        if (res.status >= 400 || res.body.empty()) continue;
        auto pos = res.body.find(".gkg.csv.zip");
        if (pos == std::string::npos) pos = res.body.find(".gkg.CSV.zip");
        if (pos == std::string::npos || pos < 14) continue;
        std::string stamp = res.body.substr(pos - 14, 14);
        bool digits = true;
        for (char c : stamp) if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
        if (digits) return stamp;
    }
    return "20260923213000";
}

static std::string gkg_country_topic(const std::string& loc, const std::string& v2,
                                     const std::map<std::string, int>& topic_counts, int per_topic) {
    bool usa = v2.find("#US#") != std::string::npos || loc.find("United States") != std::string::npos;
    bool de = v2.find("#GM#") != std::string::npos || loc.find("Germany") != std::string::npos;
    bool au = loc.find("Australia") != std::string::npos || v2.find("Australia#AS#") != std::string::npos ||
              v2.find("#AS#AS") != std::string::npos;
    struct Cand {
        const char* topic;
        bool hit;
    };
    const Cand cs[] = {{SHARED_TOPIC_USA, usa}, {SHARED_TOPIC_GERMANY, de}, {SHARED_TOPIC_AUSTRALIA, au}};
    int hits = 0;
    const char* only = nullptr;
    for (const auto& c : cs) {
        if (!c.hit) continue;
        hits++;
        only = c.topic;
    }
    if (hits == 1 && topic_count_of(topic_counts, only) < per_topic) return only;
    if (hits > 1) {
        int best_need = 0;
        const char* best = nullptr;
        for (const auto& c : cs) {
            if (!c.hit) continue;
            int need = per_topic - topic_count_of(topic_counts, c.topic);
            if (need > best_need) {
                best_need = need;
                best = c.topic;
            }
        }
        if (best) return best;
    }
    return "";
}

static std::string title_from_news_url(const std::string& url, const std::string& source) {
    auto path = url_path(url);
    std::string slug;
    if (path && path->size() > 1) {
        auto slash = path->rfind('/');
        slug = slash == std::string::npos ? path->substr(1) : path->substr(slash + 1);
        slug = url_decode(slug);
        for (char& c : slug) {
            if (c == '-' || c == '_') c = ' ';
        }
        if (slug.size() > 80) slug.resize(80);
    }
    if (slug.empty()) return source.empty() ? url : source;
    if (!source.empty()) return source + ": " + slug;
    return slug;
}

static void harvest_gdelt_gkg(const Config& cfg, GdeltCrawlStats& stats, std::unordered_set<std::string>& already,
                              std::unordered_set<std::string>& stored_keys, std::map<std::string, int>& topic_counts,
                              int per_topic, const std::function<void(const GdeltPage&)>& on_page,
                              std::string& last_error) {
    if (!country_topics_need_pages(topic_counts, per_topic)) return;
    std::string stamp = latest_gkg_stamp(cfg);
    std::cerr << "Harvesting GDELT GKG files from data.gdeltproject.org (stamp " << stamp << "; have USA="
              << topic_count_of(topic_counts, SHARED_TOPIC_USA) << " Germany="
              << topic_count_of(topic_counts, SHARED_TOPIC_GERMANY) << " Australia="
              << topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) << ").\n"
              << std::flush;
    const int delay = std::max(cfg.gdelt_delay_ms, 4000);
    const int max_files = 160;
    int empty_files = 0;
    for (int i = 0; i < max_files && country_topics_need_pages(topic_counts, per_topic) && !stamp.empty(); ++i) {
        if (i > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::string http = "http://data.gdeltproject.org/gdeltv2/" + stamp + ".gkg.csv.zip";
        std::string https = "https://data.gdeltproject.org/gdeltv2/" + stamp + ".gkg.csv.zip";
        stats.fetched++;
        auto res = http_get(http, UA, std::max(cfg.gdelt_timeout_ms, 60000), 40'000'000);
        if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
            res = http_get(https, UA, std::max(cfg.gdelt_timeout_ms, 60000), 40'000'000);
        }
        if (res.status == 429 || res.status == 503) {
            last_error = "GKG HTTP " + std::to_string(res.status);
            std::cerr << "GDELT GKG 429/503, backoff 20s\n" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(20000));
            continue;
        }
        if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
            stats.failed++;
            last_error = res.error.empty() ? ("GKG HTTP " + std::to_string(res.status)) : res.error;
            stamp = prev_gkg_stamp(stamp);
            empty_files++;
            if (empty_files > 8) break;
            continue;
        }
        std::string csv;
        try {
            csv = unzip_first_file(res.body);
        } catch (...) {
            stats.failed++;
            stamp = prev_gkg_stamp(stamp);
            continue;
        }
        res.body.clear();
        res.body.shrink_to_fit();
        if (csv.empty()) {
            stats.failed++;
            stamp = prev_gkg_stamp(stamp);
            continue;
        }
        int file_stored = 0;
        size_t pos = 0;
        while (pos < csv.size() && country_topics_need_pages(topic_counts, per_topic)) {
            size_t nl = csv.find('\n', pos);
            std::string line = csv.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? csv.size() : nl + 1;
            if (line.empty()) continue;
            auto f = split_tab(line);
            if (f.size() < 11) continue;
            std::string url = trim(f[4]);
            if (url.empty() || url.find("http") != 0) continue;
            auto canon = normalize_url(url);
            std::string stored_url = canon.value_or(url);
            auto key = dedup_key(stored_url);
            if (key && stored_keys.count(*key)) continue;
            std::string topic = gkg_country_topic(f[9], f[10], topic_counts, per_topic);
            if (topic.empty()) continue;
            std::string source = trim(f[3]);
            std::string title = title_from_news_url(stored_url, source);
            std::string text = metadata_text(title, stored_url, f[1], source, topic, "English", topic);
            if (store_gdelt_page(stats, already, stored_keys, topic_counts, per_topic, stored_url, title, text, topic,
                                 gdelt_date_iso(f[1]), on_page, last_error)) {
                file_stored++;
            }
        }
        std::cerr << "GDELT GKG " << stamp << " stored +" << file_stored << " (USA="
                  << topic_count_of(topic_counts, SHARED_TOPIC_USA) << " Germany="
                  << topic_count_of(topic_counts, SHARED_TOPIC_GERMANY) << " Australia="
                  << topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) << ")\n"
                  << std::flush;
        stamp = prev_gkg_stamp(stamp);
        if (file_stored == 0) {
            empty_files++;
            if (empty_files >= 6) {
                for (int skip = 0; skip < 24 && !stamp.empty(); ++skip) stamp = prev_gkg_stamp(stamp);
                empty_files = 0;
                std::cerr << "GDELT GKG skipping ahead to " << stamp << " after duplicate windows.\n"
                          << std::flush;
            }
        } else {
            empty_files = 0;
        }
    }
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
    if (g_doc_api_cool) return;
    static const char* spans[] = {"3m"};
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

    int now_y = 2026, now_m = 9, now_d = 23;
    for (int year = 2017; year <= now_y && !g_doc_api_cool && topic_count_of(topic_counts, topic) < per_topic; ++year) {
        std::string start = ymdhms(year, 1, 1, 0, 0, 0);
        std::string end = (year == now_y) ? ymdhms(now_y, now_m, now_d, 23, 59, 59)
                                         : ymdhms(year, 12, 31, 23, 59, 59);
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
        harvest_gdelt_gkg(cfg, stats, already, stored_keys, topic_counts, per_topic, on_page, last_error);
    }
    if (country_topics_need_pages(topic_counts, per_topic) && !g_doc_api_cool) {
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
