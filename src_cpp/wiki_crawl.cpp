#include "wiki_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "util.hpp"

#include "json.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <regex>
#include <thread>
#include <unordered_set>

namespace kos {

static const char* HOST = "en.wikipedia.org";
static const char* UA =
    "KnowledgeOS-WikiIndia/1.0 (local educational corpus ingest; en.wikipedia.org India collection)";

static const char* SKIP_NS[] = {
    "special", "file", "image", "talk", "user", "wikipedia", "wp", "help", "template", "module",
    "mediawiki", "draft", "timedtext", "media", "education_program", "gadget", "gadget_definition",
    "user_talk", "wikipedia_talk", "file_talk", "category_talk", "portal_talk", "template_talk",
    "help_talk", "draft_talk"
};

static std::string namespace_of(const std::string& title) {
    auto colon = title.find(':');
    if (colon == std::string::npos || colon == 0) return "";
    std::string ns = ascii_lower(title.substr(0, colon));
    for (char& c : ns) if (c == ' ') c = '_';
    return ns;
}

static bool skip_namespace(const std::string& title) {
    std::string ns = namespace_of(title);
    if (ns.empty()) return false;
    if (ns.size() >= 5 && ns.rfind("_talk") == ns.size() - 5) return true;
    if (ns == "talk") return true;
    for (const char* s : SKIP_NS) if (ns == s) return true;
    return false;
}

static bool is_hub_namespace(const std::string& title) {
    std::string ns = namespace_of(title);
    return ns == "category" || ns == "portal";
}

static bool is_wiki_path(const std::string& url) {
    auto p = url_path(url);
    return p && p->rfind("/wiki/", 0) == 0 && p->size() > 6;
}

std::optional<std::string> wiki_title_from_url(const std::string& url) {
    auto p = url_path(url);
    if (!p || p->rfind("/wiki/", 0) != 0) return std::nullopt;
    std::string title = p->substr(6);
    title = url_decode(title);
    for (char& c : title) if (c == '+') c = ' ';
    return title;
}

std::optional<std::string> canonicalize_wiki_url(const std::string& raw) {
    auto n = normalize_url(raw);
    if (!n) return std::nullopt;
    auto h = host_of(*n);
    if (!h) return std::nullopt;
    std::string host = ascii_lower(*h);
    if (host.rfind("www.", 0) == 0) host = host.substr(4);
    if (host == "en.m.wikipedia.org") host = HOST;
    if (host != HOST) return n;
    auto path = url_path(*n);
    std::string p = path ? *path : "/";
    return std::string("https://") + HOST + p;
}

bool is_en_wikipedia(const std::string& url) {
    auto h = host_of(canonicalize_wiki_url(url).value_or(url));
    return h && iequals(*h, HOST);
}

bool wiki_should_follow(const std::string& url) {
    auto canonical = canonicalize_wiki_url(url);
    if (!canonical || !is_en_wikipedia(*canonical) || !is_wiki_path(*canonical)) return false;
    auto title = wiki_title_from_url(*canonical);
    if (!title || is_blank(*title) || skip_namespace(*title)) return false;
    if (is_hub_namespace(*title)) return ascii_lower(*title).find("india") != std::string::npos;
    return true;
}

bool is_main_namespace_article(const std::string& url) {
    auto title = wiki_title_from_url(canonicalize_wiki_url(url).value_or(url));
    return title && !is_blank(*title) && !skip_namespace(*title) && !is_hub_namespace(*title);
}

std::string article_title_from_raw(const std::string& raw) {
    std::string title = trim(raw);
    if (title.empty()) return "Untitled";
    const std::string suffix = " - Wikipedia";
    if (title.size() >= suffix.size() && title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) {
        title = trim(title.substr(0, title.size() - suffix.size()));
    }
    return title.empty() ? "Untitled" : title;
}

static std::string topic_from_categories(const ParsedHtml& html) {
    std::string fallback;
    for (const auto& cat : html.category_texts) {
        if (is_blank(cat)) continue;
        std::string clipped = clip(trim(cat), 120);
        if (fallback.empty()) fallback = clipped;
        if (ascii_lower(cat).find("india") != std::string::npos) return clipped;
    }
    return fallback.empty() ? "India" : fallback;
}

static std::optional<std::string> parse_iso_date(const std::string& raw) {
    std::string value = trim(raw);
    if (value.size() >= 10 && value[4] == '-' && value[7] == '-') return value.substr(0, 10);
    return std::nullopt;
}

static std::optional<std::string> parse_lastmod_text(const std::string& blob) {
    static const std::regex lastmod(R"(last (?:edited|modified) on\s+(\d{1,2}\s+\w+\s+\d{4}))",
                                    std::regex::icase);
    static const std::regex lastmod_us(R"(last (?:edited|modified) on\s+(\w+\s+\d{1,2},\s+\d{4}))",
                                       std::regex::icase);
    std::smatch m;
    if (std::regex_search(blob, m, lastmod) || std::regex_search(blob, m, lastmod_us)) {
        // keep raw match; try ISO-ish conversion via month names
        std::string raw = m[1];
        static const char* months[] = {"january","february","march","april","may","june","july","august","september","october","november","december"};
        static const char* abbr[] = {"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
        std::string lower = ascii_lower(raw);
        int y = 0, mo = 0, d = 0;
        // d Month yyyy
        std::regex a(R"((\d{1,2})\s+([A-Za-z]+)\s+(\d{4}))");
        std::regex b(R"(([A-Za-z]+)\s+(\d{1,2}),\s+(\d{4}))");
        std::smatch mm;
        std::string month_s;
        if (std::regex_match(raw, mm, a)) {
            d = std::stoi(mm[1]);
            month_s = ascii_lower(mm[2]);
            y = std::stoi(mm[3]);
        } else if (std::regex_match(raw, mm, b)) {
            month_s = ascii_lower(mm[1]);
            d = std::stoi(mm[2]);
            y = std::stoi(mm[3]);
        }
        for (int i = 0; i < 12; ++i) {
            if (month_s == months[i] || month_s.rfind(abbr[i], 0) == 0) {
                mo = i + 1;
                break;
            }
        }
        if (y && mo && d) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, mo, d);
            return std::string(buf);
        }
    }
    return std::nullopt;
}

static std::optional<std::string> published_from(const ParsedHtml& html, const std::string& last_mod_header) {
    if (auto d = parse_lastmod_text(html.footer_blob)) return d;
    if (auto d = parse_iso_date(html.last_modified_meta)) return d;
    if (auto d = parse_iso_date(html.time_datetime)) return d;
    if (auto d = parse_iso_date(last_mod_header)) return d;
    return std::nullopt;
}

static std::vector<std::string> extract_wiki_links(const std::string& page_url, const std::string& html) {
    auto base = canonicalize_wiki_url(page_url);
    if (!base) return {};
    auto parsed = parse_html(html);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& href : parsed.hrefs) {
        auto resolved = resolve_url(*base, href);
        if (!resolved) continue;
        auto can = canonicalize_wiki_url(*resolved);
        if (!can || !wiki_should_follow(*can) || *can == *base) continue;
        if (seen.insert(*can).second) out.push_back(*can);
    }
    return out;
}

WikiCrawlStats crawl_wikipedia_india(const Config& cfg, std::unordered_set<std::string>& already,
                                     const std::function<void(const WikiPage&)>& on_page) {
    static const char* seeds[] = {
        "https://en.wikipedia.org/wiki/India",
        "https://en.wikipedia.org/wiki/Portal:India",
        "https://en.wikipedia.org/wiki/Category:India"
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
        auto key = dedup_key(canonicalize_wiki_url(url).value_or(url));
        if (key) stored_keys.insert(*key);
    }
    for (const char* seed : seeds) {
        auto n = canonicalize_wiki_url(seed);
        auto key = n ? dedup_key(*n) : std::nullopt;
        if (!key) continue;
        seen.insert(*key);
        queue.push_back({*n, 0, true});
    }

    WikiCrawlStats stats;
    stats.stored = static_cast<int>(stored_keys.size());
    int seed_failures = 0;
    std::string last_error;

    while (!queue.empty() && stats.stored < cfg.wikipedia_max_pages) {
        Item item = queue.front();
        queue.pop_front();
        if (stats.fetched > 0 && cfg.wikipedia_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.wikipedia_delay_ms));
        }
        stats.fetched++;
        auto page = http_get(item.url, UA, cfg.wikipedia_timeout_ms, 5'000'000);
        if (page.status == 429 || page.status == 503) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg.wikipedia_delay_ms, 300)));
            page = http_get(item.url, UA, cfg.wikipedia_timeout_ms, 5'000'000);
        }
        if (page.status >= 400 || page.body.empty() || !page.error.empty()) {
            stats.failed++;
            last_error = page.error.empty() ? ("HTTP " + std::to_string(page.status)) : page.error;
            if (item.seed) seed_failures++;
            continue;
        }
        std::string ctype = ascii_lower(page.content_type);
        if (!ctype.empty() && ctype.find("html") == std::string::npos && ctype.find("xml") == std::string::npos) {
            stats.failed++;
            continue;
        }
        std::string stored_url = page.final_url.empty() ? item.url : canonicalize_wiki_url(page.final_url).value_or(item.url);
        if (!is_en_wikipedia(stored_url)) {
            stats.failed++;
            continue;
        }
        auto stored_key = dedup_key(stored_url);
        if (stored_key) seen.insert(*stored_key);

        auto html = parse_html(page.body);
        std::string title = article_title_from_raw(html.title);
        std::string text = html.wiki_text;
        bool storeable = is_main_namespace_article(stored_url);
        if (storeable && !is_blank(text)) {
            bool already_have = stored_key && stored_keys.count(*stored_key);
            if (!already_have) {
                WikiPage wp;
                wp.url = stored_url;
                wp.title = title;
                wp.text = text;
                wp.topic = topic_from_categories(html);
                auto date = published_from(html, page.last_modified);
                wp.published_at = date.value_or("");
                on_page(wp);
                stats.stored++;
                if (stored_key) stored_keys.insert(*stored_key);
                already.insert(stored_url);
            }
        } else if (item.seed && storeable) {
            stats.failed++;
            seed_failures++;
            last_error = "Seed page had no extractable article text: " + stored_url;
        }
        if (item.depth >= cfg.wikipedia_max_depth) continue;
        for (const auto& link : extract_wiki_links(stored_url, page.body)) {
            auto key = dedup_key(link);
            if (!key || !seen.insert(*key).second) continue;
            if (!wiki_should_follow(link)) {
                seen.erase(*key);
                continue;
            }
            queue.push_back({link, item.depth + 1, false});
        }
    }

    if (stats.stored == 0 && already.empty()) {
        stats.error = "Could not reach English Wikipedia (en.wikipedia.org) from the India seeds. "
                      "Network may be blocking Wikipedia. Seeds: https://en.wikipedia.org/wiki/India. Last error: " +
                      (last_error.empty() ? std::string("no article text could be extracted") : last_error) +
                      ". Seed failures: " + std::to_string(seed_failures) + ".";
    }
    return stats;
}

std::map<std::string, std::string> parse_revision_query(const std::string& body) {
    std::map<std::string, std::string> out;
    if (body.empty()) return out;
    try {
        auto root = nlohmann::json::parse(body);
        auto query = root["query"];
        std::map<std::string, std::string> aliases;
        if (query.contains("normalized")) {
            for (const auto& n : query["normalized"]) {
                std::string from = n.value("from", "");
                std::string to = n.value("to", "");
                if (!from.empty() && !to.empty()) aliases[from] = to;
            }
        }
        if (query.contains("redirects")) {
            for (const auto& n : query["redirects"]) {
                std::string from = n.value("from", "");
                std::string to = n.value("to", "");
                if (!from.empty() && !to.empty()) aliases[from] = to;
            }
        }
        std::map<std::string, std::string> by_canonical;
        if (query.contains("pages")) {
            for (const auto& page : query["pages"]) {
                if (page.value("missing", false)) continue;
                std::string title = page.value("title", "");
                if (!page.contains("revisions") || !page["revisions"].is_array() || page["revisions"].empty()) continue;
                std::string ts = page["revisions"][0].value("timestamp", "");
                auto date = parse_iso_date(ts);
                if (date && !title.empty()) {
                    by_canonical[title] = *date;
                    out[title] = *date;
                }
            }
        }
        for (const auto& [from, to] : aliases) {
            auto it = by_canonical.find(to);
            if (it != by_canonical.end()) out[from] = it->second;
        }
    } catch (...) {
    }
    return out;
}

std::map<std::string, std::string> fetch_last_revisions(const std::vector<std::string>& titles, const Config& cfg) {
    std::map<std::string, std::string> out;
    std::vector<std::string> unique;
    std::unordered_set<std::string> seen;
    for (const auto& title : titles) {
        if (is_blank(title)) continue;
        std::string key = title;
        for (char& c : key) if (c == '_') c = ' ';
        key = trim(key);
        if (seen.insert(ascii_lower(key)).second) unique.push_back(key);
    }
    for (size_t i = 0; i < unique.size(); i += 50) {
        std::string joined;
        for (size_t j = i; j < unique.size() && j < i + 50; ++j) {
            if (!joined.empty()) joined += "|";
            joined += unique[j];
        }
        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                          "&prop=revisions&rvprop=timestamp&redirects=1&titles=" +
                          url_encode(joined);
        auto res = http_get(api, UA, cfg.wikipedia_timeout_ms, 2'000'000);
        if (res.status < 400 && !res.body.empty()) {
            auto part = parse_revision_query(res.body);
            out.insert(part.begin(), part.end());
        }
        if (i + 50 < unique.size() && cfg.wikipedia_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.wikipedia_delay_ms));
        }
    }
    return out;
}

}  // namespace kos
