#include "wiki_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "util.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <deque>
#include <iostream>
#include <ostream>
#include <map>
#include <regex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace kos {

static const char* HOST = "en.wikipedia.org";
static const char* UA =
    "KnowledgeOS-Wiki/1.0 (local educational corpus ingest; en.wikipedia.org general collection)";

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
    if (is_hub_namespace(*title)) return true;
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

static std::string map_india_topic(const std::string& blob) {
    std::string lower = ascii_lower(blob);
    struct Rule {
        const char* needle;
        const char* label;
    };
    static const Rule rules[] = {
        {"states and union territor", "Geography of India"},
        {"union territor", "Geography of India"},
        {"states of india", "Geography of India"},
        {"districts of", "Geography of India"},
        {"cities in india", "Geography of India"},
        {"cities and towns in", "Geography of India"},
        {"populated places in", "Geography of India"},
        {"metropolitan cities", "Geography of India"},
        {"capitals in india", "Geography of India"},
        {"municipal corporat", "Geography of India"},
        {"andhra pradesh", "Geography of India"},
        {"arunachal", "Geography of India"},
        {"assam", "Geography of India"},
        {"bihar", "Geography of India"},
        {"chhattisgarh", "Geography of India"},
        {"gujarat", "Geography of India"},
        {"haryana", "Geography of India"},
        {"himachal", "Geography of India"},
        {"jharkhand", "Geography of India"},
        {"karnataka", "Geography of India"},
        {"kerala", "Geography of India"},
        {"madhya pradesh", "Geography of India"},
        {"maharashtra", "Geography of India"},
        {"manipur", "Geography of India"},
        {"meghalaya", "Geography of India"},
        {"mizoram", "Geography of India"},
        {"nagaland", "Geography of India"},
        {"odisha", "Geography of India"},
        {"orissa", "Geography of India"},
        {"punjab", "Geography of India"},
        {"rajasthan", "Geography of India"},
        {"sikkim", "Geography of India"},
        {"tamil nadu", "Geography of India"},
        {"telangana", "Geography of India"},
        {"tripura", "Geography of India"},
        {"uttar pradesh", "Geography of India"},
        {"uttarakhand", "Geography of India"},
        {"west bengal", "Geography of India"},
        {"new delhi", "Geography of India"},
        {"mumbai", "Geography of India"},
        {"kolkata", "Geography of India"},
        {"chennai", "Geography of India"},
        {"bengaluru", "Geography of India"},
        {"bangalore", "Geography of India"},
        {"hyderabad", "Geography of India"},
        {"ahmedabad", "Geography of India"},
        {"pune", "Geography of India"},
        {"jaipur", "Geography of India"},
        {"lucknow", "Geography of India"},
        {"ganges", "Geography of India"},
        {"yamuna", "Geography of India"},
        {"brahmaputra", "Geography of India"},
        {"thar desert", "Geography of India"},
        {"history of india", "History of India"},
        {"ancient india", "History of India"},
        {"medieval india", "History of India"},
        {"british raj", "History of India"},
        {"british india", "History of India"},
        {"company rule", "History of India"},
        {"mughal", "History of India"},
        {"maratha", "History of India"},
        {"independence movement", "History of India"},
        {"partition of india", "History of India"},
        {"indian independence", "History of India"},
        {"politics of india", "Politics of India"},
        {"political parties in india", "Politics of India"},
        {"elections in india", "Politics of India"},
        {"lok sabha", "Politics of India"},
        {"rajya sabha", "Politics of India"},
        {"prime minister of india", "Politics of India"},
        {"government of india", "Politics of India"},
        {"indian national congress", "Politics of India"},
        {"bharatiya janata", "Politics of India"},
        {"law of india", "Politics of India"},
        {"constitution of india", "Politics of India"},
        {"supreme court of india", "Politics of India"},
        {"military of india", "Politics of India"},
        {"indian army", "Politics of India"},
        {"armed forces of india", "Politics of India"},
        {"economy of india", "Economy of India"},
        {"companies of india", "Economy of India"},
        {"companies based in", "Economy of India"},
        {"economic history of india", "Economy of India"},
        {"industry in india", "Economy of India"},
        {"agriculture in india", "Economy of India"},
        {"transport in india", "Economy of India"},
        {"indian railways", "Economy of India"},
        {"airports in india", "Economy of India"},
        {"highways in india", "Economy of India"},
        {"ports of india", "Economy of India"},
        {"sport in india", "Culture of India"},
        {"sports in india", "Culture of India"},
        {"cricket in india", "Culture of India"},
        {"indian premier league", "Culture of India"},
        {"hockey in india", "Culture of India"},
        {"cinema of india", "Culture of India"},
        {"bollywood", "Culture of India"},
        {"film in india", "Culture of India"},
        {"indian films", "Culture of India"},
        {"cuisine of india", "Culture of India"},
        {"indian cuisine", "Culture of India"},
        {"languages of india", "Culture of India"},
        {"indo-aryan languages", "Culture of India"},
        {"dravidian languages", "Culture of India"},
        {"religion in india", "Culture of India"},
        {"hinduism in india", "Culture of India"},
        {"islam in india", "Culture of India"},
        {"sikhism", "Culture of India"},
        {"culture of india", "Culture of India"},
        {"indian literature", "Culture of India"},
        {"music of india", "Culture of India"},
        {"tributaries of", "Rivers of India"},
        {"rivers of india", "Rivers of India"},
        {"rivers in india", "Rivers of India"},
        {"river systems of", "Rivers of India"},
        {"drainage of india", "Rivers of India"},
        {"ganges", "Rivers of India"},
        {"ganga river", "Rivers of India"},
        {"brahmaputra", "Rivers of India"},
        {"yamuna", "Rivers of India"},
        {"godavari", "Rivers of India"},
        {"narmada", "Rivers of India"},
        {"kaveri", "Rivers of India"},
        {"cauvery", "Rivers of India"},
        {"mahanadi", "Rivers of India"},
        {"sutlej", "Rivers of India"},
        {"hooghly", "Rivers of India"},
        {"mountain ranges of india", "Geography of India"},
        {"himalay", "Geography of India"},
        {"climate of india", "Geography of India"},
        {"monsoon", "Geography of India"},
        {"geography of india", "Geography of India"},
        {"indian ocean", "Geography of India"},
        {"western ghats", "Geography of India"},
        {"indo-gangetic", "Geography of India"},
        {"national parks of india", "Geography of India"},
        {"education in india", "Science and society in India"},
        {"universities in india", "Science and society in India"},
        {"medicine in india", "Science and society in India"},
        {"health in india", "Science and society in India"},
        {"science and technology in india", "Science and society in India"},
        {"demographics of india", "Science and society in India"},
        {"indian people", "Science and society in India"},
    };
    for (const auto& r : rules) {
        if (lower.find(r.needle) != std::string::npos) return r.label;
    }
    return "";
}

static std::string map_general_topic(const std::string& blob) {
    std::string lower = ascii_lower(blob);
    struct Rule {
        const char* needle;
        const char* label;
    };
    static const Rule rules[] = {
        {"mathematic", "Mathematics"},
        {"physics", "Science"},
        {"chemistry", "Science"},
        {"biolog", "Science"},
        {"astronom", "Science"},
        {"science", "Science"},
        {"computer", "Technology"},
        {"software", "Technology"},
        {"technolog", "Technology"},
        {"engineer", "Technology"},
        {"history", "History"},
        {"war ", "History"},
        {"empire", "History"},
        {"geography", "Geography"},
        {"cities", "Geography"},
        {"countries", "Geography"},
        {"politic", "Politics"},
        {"government", "Politics"},
        {"election", "Politics"},
        {"econom", "Economy"},
        {"philosoph", "Philosophy"},
        {"religion", "Religion"},
        {"literatur", "Arts and culture"},
        {"music", "Arts and culture"},
        {"film", "Arts and culture"},
        {"art ", "Arts and culture"},
        {"sport", "Sports"},
        {"living people", "Biography"},
        {"births", "Biography"},
        {"people", "Biography"},
    };
    for (const auto& r : rules) {
        if (lower.find(r.needle) != std::string::npos) return r.label;
    }
    return "";
}

static std::string topic_from_categories(const ParsedHtml& html, const std::string& title) {
    std::string blob = title;
    std::string fallback;
    for (const auto& cat : html.category_texts) {
        if (is_blank(cat)) continue;
        if (!blob.empty()) blob += " | ";
        blob += cat;
        std::string clipped = clip(trim(cat), 120);
        std::string lower = ascii_lower(clipped);
        if (fallback.empty()) fallback = clipped;
        if (lower.find("india") != std::string::npos &&
            (fallback.find("India") == std::string::npos || clipped.size() < fallback.size())) {
            fallback = clipped;
        }
    }
    std::string mapped = map_india_topic(blob);
    if (!mapped.empty()) return mapped;
    mapped = map_general_topic(blob);
    if (!mapped.empty()) return mapped;
    if (!fallback.empty()) return fallback;
    return "General";
}

static bool contains_word(const std::string& lower, const std::string& word) {
    if (word.empty()) return false;
    size_t pos = 0;
    while ((pos = lower.find(word, pos)) != std::string::npos) {
        bool left = pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
        bool right = pos + word.size() >= lower.size() ||
                     !std::isalnum(static_cast<unsigned char>(lower[pos + word.size()]));
        if (left && right) return true;
        pos += word.size();
    }
    return false;
}

static bool mentions_india(const std::string& lower) {
    return lower.find("india") != std::string::npos || lower.find("indian") != std::string::npos ||
           lower.find("bharat") != std::string::npos;
}

static bool mentions_usa(const std::string& lower) {
    return lower.find("united states") != std::string::npos || lower.find("u.s.a") != std::string::npos ||
           lower.find("u.s.") != std::string::npos || contains_word(lower, "usa");
}

static bool mentions_germany(const std::string& lower) {
    return lower.find("germany") != std::string::npos || lower.find("deutschland") != std::string::npos ||
           contains_word(lower, "german") || contains_word(lower, "germans");
}

static bool mentions_australia(const std::string& lower) {
    return lower.find("australia") != std::string::npos || lower.find("australian") != std::string::npos;
}

static std::string kept_shared_topic(const std::string& raw) {
    std::string t = trim(raw);
    if (t.empty()) return "";
    if (iequals(t, SHARED_TOPIC_INDIA)) return SHARED_TOPIC_INDIA;
    if (iequals(t, SHARED_TOPIC_GEO_INDIA)) return SHARED_TOPIC_GEO_INDIA;
    if (iequals(t, SHARED_TOPIC_USA) || iequals(t, "USA") || iequals(t, "US") ||
        iequals(t, "United States of America")) {
        return SHARED_TOPIC_USA;
    }
    if (iequals(t, SHARED_TOPIC_GERMANY)) return SHARED_TOPIC_GERMANY;
    if (iequals(t, SHARED_TOPIC_AUSTRALIA)) return SHARED_TOPIC_AUSTRALIA;
    return "";
}

std::string classify_country_topic(const std::string& title, const std::string& text) {
    if (auto kept = kept_shared_topic(title); !kept.empty() && is_shared_country_topic(kept)) return kept;
    std::string head = text.size() > 4000 ? text.substr(0, 4000) : text;
    std::string tlow = ascii_lower(title);
    std::string blob = ascii_lower(std::string(title) + " | " + head);
    bool usa = mentions_usa(tlow) || mentions_usa(blob);
    bool de = mentions_germany(tlow) || mentions_germany(blob);
    bool au = mentions_australia(tlow) || mentions_australia(blob);
    int hits = (usa ? 1 : 0) + (de ? 1 : 0) + (au ? 1 : 0);
    if (mentions_usa(tlow) && !mentions_germany(tlow) && !mentions_australia(tlow)) return SHARED_TOPIC_USA;
    if (mentions_germany(tlow) && !mentions_usa(tlow) && !mentions_australia(tlow)) return SHARED_TOPIC_GERMANY;
    if (mentions_australia(tlow) && !mentions_usa(tlow) && !mentions_germany(tlow)) return SHARED_TOPIC_AUSTRALIA;
    if (hits == 1) {
        if (usa) return SHARED_TOPIC_USA;
        if (de) return SHARED_TOPIC_GERMANY;
        return SHARED_TOPIC_AUSTRALIA;
    }
    return "";
}

static std::string collapse_india_topic(const std::string& mapped) {
    if (mapped.empty()) return "";
    if (iequals(mapped, SHARED_TOPIC_GEO_INDIA)) return SHARED_TOPIC_GEO_INDIA;
    return SHARED_TOPIC_INDIA;
}

std::string canonical_wiki_topic(const std::string& topic, const std::string& title) {
    if (auto kept = kept_shared_topic(topic); !kept.empty()) return kept;
    if (auto kept = kept_shared_topic(title); !kept.empty()) return kept;
    std::string country = classify_country_topic(title, topic);
    if (!country.empty()) return country;
    std::string mapped = collapse_india_topic(map_india_topic(std::string(title) + " | " + topic));
    if (!mapped.empty()) return mapped;
    mapped = collapse_india_topic(map_india_topic(title));
    if (!mapped.empty()) return mapped;
    mapped = collapse_india_topic(map_india_topic(topic));
    if (!mapped.empty()) return mapped;
    if (!trim(topic).empty()) return trim(topic);
    return "General";
}

bool is_shared_india_topic(const std::string& topic) {
    std::string t = trim(topic);
    return iequals(t, SHARED_TOPIC_INDIA) || iequals(t, SHARED_TOPIC_GEO_INDIA);
}

bool is_shared_country_topic(const std::string& topic) {
    std::string t = kept_shared_topic(topic);
    return iequals(t, SHARED_TOPIC_USA) || iequals(t, SHARED_TOPIC_GERMANY) ||
           iequals(t, SHARED_TOPIC_AUSTRALIA);
}

std::string classify_shared_india_topic(const std::string& title, const std::string& text) {
    std::string head = text.size() > 4000 ? text.substr(0, 4000) : text;
    std::string blob = title + " | " + head;
    std::string mapped = map_india_topic(blob);
    if (iequals(mapped, SHARED_TOPIC_GEO_INDIA)) return SHARED_TOPIC_GEO_INDIA;
    if (!mapped.empty() || mentions_india(ascii_lower(blob))) return SHARED_TOPIC_INDIA;
    return "";
}

bool is_wikipedia_name(const std::string& name) {
    std::string lower = ascii_lower(trim(name));
    return lower == "wikipedia" || lower == "wikipedia india" || lower == "wikipedia sample";
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

static std::string wiki_article_url(std::string title) {
    title = trim(title);
    for (char& c : title) if (c == ' ') c = '_';
    return std::string("https://") + HOST + "/wiki/" + url_encode(title);
}

static bool looks_like_disambiguation(const std::string& title, const std::string& text) {
    if (ascii_lower(title).find("(disambiguation)") != std::string::npos) return true;
    std::string head = ascii_lower(text.substr(0, std::min<size_t>(text.size(), 180)));
    return head.find("may refer to") != std::string::npos && count_words(text) < 120;
}

std::map<std::string, std::string> parse_extract_query(const std::string& body) {
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
                std::string extract = page.value("extract", "");
                if (!title.empty() && !is_blank(extract)) {
                    by_canonical[title] = extract;
                    out[title] = extract;
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

std::optional<WikiExtract> fetch_wiki_extract(const std::string& title, const Config& cfg) {
    std::string key = title;
    for (char& c : key) if (c == '_') c = ' ';
    key = trim(key);
    if (key.empty()) return std::nullopt;
    std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                      "&prop=extracts&explaintext=1&exsectionformat=plain&redirects=1&exlimit=1&titles=" +
                      url_encode(key);
    auto res = http_get(api, UA, cfg.wikipedia_timeout_ms, 2'000'000);
    if (res.status >= 400 || res.body.empty()) return std::nullopt;
    auto texts = parse_extract_query(res.body);
    if (texts.empty()) return std::nullopt;
    WikiExtract out;
    out.title = key;
    try {
        auto root = nlohmann::json::parse(res.body);
        if (root.contains("query") && root["query"].contains("pages")) {
            for (const auto& page : root["query"]["pages"]) {
                if (page.value("missing", false)) continue;
                std::string t = page.value("title", "");
                if (!t.empty()) out.title = t;
            }
        }
    } catch (...) {
    }
    auto it = texts.find(key);
    if (it == texts.end()) it = texts.find(out.title);
    if (it == texts.end()) {
        it = texts.begin();
        for (auto p = texts.begin(); p != texts.end(); ++p) {
            if (p->second.size() > it->second.size()) it = p;
        }
    }
    out.text = it->second;
    if (is_blank(out.text)) return std::nullopt;
    return out;
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

static std::string lookup_revision_date(const std::map<std::string, std::string>& dates, const std::string& title) {
    std::string key = title;
    for (char& c : key) if (c == '_') c = ' ';
    key = trim(key);
    auto it = dates.find(key);
    if (it != dates.end()) return it->second;
    std::string needle = ascii_lower(key);
    for (const auto& [k, v] : dates) {
        if (ascii_lower(k) == needle) return v;
    }
    return "";
}

static bool campaign_mentions(const std::string& topic, const std::string& blob) {
    if (iequals(topic, SHARED_TOPIC_INDIA) || iequals(topic, SHARED_TOPIC_GEO_INDIA)) {
        return mentions_india(ascii_lower(blob));
    }
    if (iequals(topic, SHARED_TOPIC_USA)) return mentions_usa(blob);
    if (iequals(topic, SHARED_TOPIC_GERMANY)) return mentions_germany(blob);
    if (iequals(topic, SHARED_TOPIC_AUSTRALIA)) return mentions_australia(blob);
    return false;
}

static int topic_count_of(const std::map<std::string, int>& counts, const std::string& topic) {
    auto it = counts.find(topic);
    return it == counts.end() ? 0 : it->second;
}

static HttpResponse wiki_api_get(const std::string& api, const Config& cfg) {
    auto res = http_get(api, UA, cfg.wikipedia_timeout_ms, 8'000'000);
    if (res.status == 429 || res.status == 503) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg.wikipedia_delay_ms, 2500)));
        res = http_get(api, UA, cfg.wikipedia_timeout_ms, 8'000'000);
    }
    return res;
}

static bool emit_wiki_page(const std::string& title, const std::string& text, const char* topic, int per_topic,
                           std::unordered_set<std::string>& already, std::unordered_set<std::string>& stored_keys,
                           std::map<std::string, int>& topic_counts, WikiCrawlStats& stats,
                           const std::function<void(const WikiPage&)>& on_page) {
    if (topic_count_of(topic_counts, topic) >= per_topic) return false;
    if (skip_namespace(title) || is_hub_namespace(title)) return false;
    if (count_words(text) < WIKI_MIN_ARTICLE_WORDS || looks_like_disambiguation(title, text)) return false;
    std::string url = wiki_article_url(title);
    auto key = dedup_key(url);
    if (key && stored_keys.count(*key)) return false;
    WikiPage wp;
    wp.url = url;
    wp.title = title;
    wp.text = text;
    wp.topic = topic;
    try {
        on_page(wp);
        stats.stored++;
        topic_counts[topic]++;
        if (key) stored_keys.insert(*key);
        already.insert(url);
        if (stats.stored <= 5 || stats.stored % 10 == 0 || topic_counts[topic] % 25 == 0) {
            std::cerr << "Wikipedia stored " << stats.stored << " [" << topic << " " << topic_counts[topic] << "/"
                      << per_topic << "] (" << title << ")\n"
                      << std::flush;
        }
        return true;
    } catch (const std::exception& e) {
        stats.failed++;
        std::cerr << "Wikipedia store failed: " << e.what() << "\n";
        return false;
    }
}

static void harvest_extract_titles(const Config& cfg, const char* topic, const std::vector<std::string>& titles,
                                   int per_topic, std::unordered_set<std::string>& already,
                                   std::unordered_set<std::string>& stored_keys, std::map<std::string, int>& topic_counts,
                                   WikiCrawlStats& stats, const std::function<void(const WikiPage&)>& on_page) {
    const int batch = 20;
    for (size_t i = 0; i < titles.size() && topic_count_of(topic_counts, topic) < per_topic; ++i) {
        std::string joined;
        int n = 0;
        while (i < titles.size() && n < batch) {
            std::string t = titles[i];
            for (char& c : t) if (c == '_') c = ' ';
            t = trim(t);
            ++i;
            if (t.empty() || skip_namespace(t) || is_hub_namespace(t)) continue;
            auto key = dedup_key(wiki_article_url(t));
            if (key && stored_keys.count(*key)) continue;
            if (!joined.empty()) joined += '|';
            joined += t;
            ++n;
        }
        --i;
        if (joined.empty()) continue;
        if (cfg.wikipedia_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.wikipedia_delay_ms));
        }
        stats.fetched++;
        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                          "&prop=extracts&explaintext=1&exintro=1&exsectionformat=plain&redirects=1"
                          "&exchars=1800&exlimit=20&titles=" +
                          url_encode(joined);
        auto res = wiki_api_get(api, cfg);
        if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
            stats.failed++;
            continue;
        }
        for (const auto& [title, text] : parse_extract_query(res.body)) {
            emit_wiki_page(title, text, topic, per_topic, already, stored_keys, topic_counts, stats, on_page);
        }
    }
}

static void harvest_category_extracts(const Config& cfg, std::string cat, const char* topic, int per_topic,
                                      std::unordered_set<std::string>& already,
                                      std::unordered_set<std::string>& stored_keys,
                                      std::map<std::string, int>& topic_counts, WikiCrawlStats& stats,
                                      const std::function<void(const WikiPage&)>& on_page) {
    for (char& c : cat) if (c == '_') c = ' ';
    cat = trim(cat);
    if (cat.empty()) return;
    int before = topic_count_of(topic_counts, topic);
    std::cerr << "Wikipedia category extracts " << cat << " [" << topic << " have " << before << "]\n"
              << std::flush;
    std::vector<std::string> titles;
    std::string cont;
    for (int page = 0; page < 8 && static_cast<int>(titles.size()) < 1200 &&
                       topic_count_of(topic_counts, topic) < per_topic; ++page) {
        if (page > 0 && cfg.wikipedia_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.wikipedia_delay_ms));
        }
        stats.fetched++;
        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                          "&list=categorymembers&cmtype=page&cmnamespace=0&cmlimit=500&cmtitle=" +
                          url_encode(cat);
        if (!cont.empty()) api += "&cmcontinue=" + url_encode(cont);
        auto res = wiki_api_get(api, cfg);
        if (res.status >= 400 || res.body.empty() || !res.error.empty()) {
            stats.failed++;
            std::cerr << "Wikipedia categorymembers failed " << cat << " HTTP " << res.status << " "
                      << res.error << " bytes=" << res.body.size() << "\n"
                      << std::flush;
            if (res.status == 429 || res.status == 503) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15000));
                break;
            }
            break;
        }
        try {
            auto root = nlohmann::json::parse(res.body);
            if (root.contains("query") && root["query"].contains("categorymembers")) {
                for (const auto& m : root["query"]["categorymembers"]) {
                    std::string mt = m.value("title", "");
                    if (mt.empty() || skip_namespace(mt) || is_hub_namespace(mt)) continue;
                    titles.push_back(mt);
                }
            }
            cont.clear();
            if (root.contains("continue") && root["continue"].contains("cmcontinue")) {
                cont = root["continue"].value("cmcontinue", "");
            }
            if (cont.empty()) break;
        } catch (...) {
            std::cerr << "Wikipedia categorymembers JSON failed " << cat << " bytes=" << res.body.size() << "\n"
                      << std::flush;
            break;
        }
    }
    std::cerr << "Wikipedia queued " << titles.size() << " titles from " << cat << "\n" << std::flush;
    harvest_extract_titles(cfg, topic, titles, per_topic, already, stored_keys, topic_counts, stats, on_page);
    std::cerr << "Wikipedia " << cat << " stored +" << (topic_count_of(topic_counts, topic) - before) << " now "
              << topic_count_of(topic_counts, topic) << "\n"
              << std::flush;
}

static std::vector<std::string> fetch_subcategories(const Config& cfg, std::string cat, int max_n) {
    for (char& c : cat) if (c == '_') c = ' ';
    std::vector<std::string> out;
    std::string cont;
    for (int page = 0; page < 4 && static_cast<int>(out.size()) < max_n; ++page) {
        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                          "&list=categorymembers&cmtype=subcat&cmlimit=500&cmtitle=" +
                          url_encode(cat);
        if (!cont.empty()) api += "&cmcontinue=" + url_encode(cont);
        auto res = wiki_api_get(api, cfg);
        if (res.status >= 400 || res.body.empty()) break;
        try {
            auto root = nlohmann::json::parse(res.body);
            if (root.contains("query") && root["query"].contains("categorymembers")) {
                for (const auto& m : root["query"]["categorymembers"]) {
                    std::string mt = m.value("title", "");
                    if (!mt.empty()) out.push_back(mt);
                    if (static_cast<int>(out.size()) >= max_n) break;
                }
            }
            cont.clear();
            if (root.contains("continue") && root["continue"].contains("cmcontinue")) {
                cont = root["continue"].value("cmcontinue", "");
            }
            if (cont.empty()) break;
        } catch (...) {
            break;
        }
    }
    return out;
}

static std::vector<std::string> fetch_article_links(const Config& cfg, std::string title, int max_n) {
    for (char& c : title) if (c == '_') c = ' ';
    std::vector<std::string> out;
    std::string cont;
    for (int page = 0; page < 4 && static_cast<int>(out.size()) < max_n; ++page) {
        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                          "&prop=links&plnamespace=0&pllimit=500&redirects=1&titles=" +
                          url_encode(title);
        if (!cont.empty()) api += "&plcontinue=" + url_encode(cont);
        auto res = wiki_api_get(api, cfg);
        if (res.status >= 400 || res.body.empty()) break;
        try {
            auto root = nlohmann::json::parse(res.body);
            if (root.contains("query") && root["query"].contains("pages")) {
                for (const auto& p : root["query"]["pages"]) {
                    if (!p.contains("links")) continue;
                    for (const auto& link : p["links"]) {
                        std::string mt = link.value("title", "");
                        if (mt.empty() || skip_namespace(mt) || is_hub_namespace(mt)) continue;
                        out.push_back(mt);
                        if (static_cast<int>(out.size()) >= max_n) break;
                    }
                }
            }
            cont.clear();
            if (root.contains("continue") && root["continue"].contains("plcontinue")) {
                cont = root["continue"].value("plcontinue", "");
            }
            if (cont.empty()) break;
        } catch (...) {
            break;
        }
    }
    return out;
}

WikiCrawlStats crawl_wikipedia(const Config& cfg, std::unordered_set<std::string>& already,
                               const std::function<void(const WikiPage&)>& on_page,
                               std::map<std::string, int> topic_counts) {
    struct Campaign {
        const char* topic;
        std::vector<const char*> seeds;
    };
    static const Campaign campaigns[] = {
        {SHARED_TOPIC_USA,
         {"https://en.wikipedia.org/wiki/Category:Cities_in_California",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Texas",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Florida",
          "https://en.wikipedia.org/wiki/Category:Cities_in_New_York_(state)",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Illinois",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Pennsylvania",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Ohio",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Georgia_(U.S._state)",
          "https://en.wikipedia.org/wiki/Category:Cities_in_North_Carolina",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Michigan",
          "https://en.wikipedia.org/wiki/Category:Cities_in_New_Jersey",
          "https://en.wikipedia.org/wiki/Category:Cities_in_the_United_States_by_state",
          "https://en.wikipedia.org/wiki/Category:States_of_the_United_States",
          "https://en.wikipedia.org/wiki/United_States",
          "https://en.wikipedia.org/wiki/California",
          "https://en.wikipedia.org/wiki/Texas",
          "https://en.wikipedia.org/wiki/New_York_City",
          "https://en.wikipedia.org/wiki/Florida"}},
        {SHARED_TOPIC_GERMANY,
         {"https://en.wikipedia.org/wiki/Category:Cities_in_North_Rhine-Westphalia",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Bavaria",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Baden-Württemberg",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Lower_Saxony",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Hesse",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Germany",
          "https://en.wikipedia.org/wiki/Category:Towns_in_Germany",
          "https://en.wikipedia.org/wiki/Germany",
          "https://en.wikipedia.org/wiki/Berlin",
          "https://en.wikipedia.org/wiki/Munich",
          "https://en.wikipedia.org/wiki/Hamburg"}},
        {SHARED_TOPIC_AUSTRALIA,
         {"https://en.wikipedia.org/wiki/Category:Suburbs_of_Sydney",
          "https://en.wikipedia.org/wiki/Category:Suburbs_of_Melbourne",
          "https://en.wikipedia.org/wiki/Category:Suburbs_of_Brisbane",
          "https://en.wikipedia.org/wiki/Category:Cities_in_New_South_Wales",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Victoria_(state)",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Queensland",
          "https://en.wikipedia.org/wiki/Category:Cities_in_Australia",
          "https://en.wikipedia.org/wiki/Australia",
          "https://en.wikipedia.org/wiki/Sydney",
          "https://en.wikipedia.org/wiki/Melbourne",
          "https://en.wikipedia.org/wiki/New_South_Wales"}}
    };

    struct Item {
        std::string url;
        int depth;
        bool seed;
    };
    std::unordered_set<std::string> stored_keys;
    for (const auto& url : already) {
        auto key = dedup_key(canonicalize_wiki_url(url).value_or(url));
        if (key) stored_keys.insert(*key);
    }

    WikiCrawlStats stats;
    stats.stored = static_cast<int>(stored_keys.size());
    int seed_failures = 0;
    std::string last_error;
    const int per_topic = std::max({cfg.wikipedia_max_pages, WIKI_MIN_SHARED_PAGES, WIKI_MIN_COUNTRY_PAGES});
    const int max_fetches_each = std::max(per_topic * 8, 4000);

    for (const auto& campaign : campaigns) {
        if (topic_count_of(topic_counts, campaign.topic) >= per_topic) continue;
        std::cerr << "Wikipedia extracts " << campaign.topic << " from en.wikipedia.org (have "
                  << topic_count_of(topic_counts, campaign.topic) << ", want " << per_topic << ").\n"
                  << std::flush;
        std::vector<std::string> seed_articles;
        std::vector<std::string> seed_cats;
        for (const char* seed : campaign.seeds) {
            auto n = canonicalize_wiki_url(seed);
            if (!n) continue;
            auto title = wiki_title_from_url(*n);
            if (!title) continue;
            if (is_hub_namespace(*title)) seed_cats.push_back(*title);
            else if (!skip_namespace(*title)) seed_articles.push_back(*title);
        }
        auto skip_meta_cat = [](const std::string& title) {
            std::string low = ascii_lower(title);
            return low.find("stub") != std::string::npos || low.find("image") != std::string::npos ||
                   low.find("list") != std::string::npos || low.find("template") != std::string::npos ||
                   low.find("portal") != std::string::npos || low.find("wiki") != std::string::npos ||
                   low.find(" by county") != std::string::npos || low.find(" by borough") != std::string::npos ||
                   low.find(" by parish") != std::string::npos || low.find(" by metropolitan") != std::string::npos ||
                   low.find(" by planning") != std::string::npos ||
                   low.find("transportation") != std::string::npos ||
                   low.find("economy of") != std::string::npos ||
                   low.find("education") != std::string::npos ||
                   low.find("culture of") != std::string::npos ||
                   low.find("history of") != std::string::npos ||
                   low.find("politics of") != std::string::npos ||
                   low.find("buildings") != std::string::npos;
        };
        for (const auto& cat : seed_cats) {
            if (topic_count_of(topic_counts, campaign.topic) >= per_topic) break;
            harvest_category_extracts(cfg, cat, campaign.topic, per_topic, already, stored_keys, topic_counts,
                                      stats, on_page);
            if (topic_count_of(topic_counts, campaign.topic) >= per_topic) break;
            auto place_list_cat = [](const std::string& title) {
                std::string low = ascii_lower(title);
                return low.find("cities in") != std::string::npos || low.find("towns in") != std::string::npos ||
                       low.find("suburbs of") != std::string::npos || low.find("suburbs in") != std::string::npos ||
                       low.find("counties") != std::string::npos ||
                       low.find("populated places") != std::string::npos ||
                       low.find("states of") != std::string::npos ||
                       low.find("states and territories") != std::string::npos;
            };
            for (const auto& sub : fetch_subcategories(cfg, cat, 80)) {
                if (topic_count_of(topic_counts, campaign.topic) >= per_topic) break;
                if (skip_meta_cat(sub) || !place_list_cat(sub)) continue;
                harvest_category_extracts(cfg, sub, campaign.topic, per_topic, already, stored_keys, topic_counts,
                                          stats, on_page);
                if (topic_count_of(topic_counts, campaign.topic) >= per_topic) break;
                for (const auto& sub2 : fetch_subcategories(cfg, sub, 80)) {
                    if (topic_count_of(topic_counts, campaign.topic) >= per_topic) break;
                    if (skip_meta_cat(sub2) || !place_list_cat(sub2)) continue;
                    harvest_category_extracts(cfg, sub2, campaign.topic, per_topic, already, stored_keys,
                                              topic_counts, stats, on_page);
                }
            }
        }
        if (topic_count_of(topic_counts, campaign.topic) < per_topic) {
            harvest_extract_titles(cfg, campaign.topic, seed_articles, per_topic, already, stored_keys, topic_counts,
                                   stats, on_page);
        }
        if (topic_count_of(topic_counts, campaign.topic) < per_topic) {
            std::vector<std::string> linked;
            for (const auto& t : seed_articles) {
                auto more = fetch_article_links(cfg, t, 800);
                linked.insert(linked.end(), more.begin(), more.end());
            }
            harvest_extract_titles(cfg, campaign.topic, linked, per_topic, already, stored_keys, topic_counts, stats,
                                   on_page);
        }
        std::cerr << "Wikipedia " << campaign.topic << " after extracts "
                  << topic_count_of(topic_counts, campaign.topic) << " stored.\n"
                  << std::flush;
        if (topic_count_of(topic_counts, campaign.topic) >= per_topic) continue;

        std::deque<Item> queue;
        std::unordered_set<std::string> seen;
        for (const char* seed : campaign.seeds) {
            auto n = canonicalize_wiki_url(seed);
            auto key = n ? dedup_key(*n) : std::nullopt;
            if (!key) continue;
            seen.insert(*key);
            queue.push_back({*n, 0, true});
            if (std::string(seed).find("/wiki/Category:") != std::string::npos) {
                auto title = wiki_title_from_url(*n);
                if (title) {
                    std::string seed_title = *title;
                    for (char& c : seed_title) if (c == '_') c = ' ';
                    std::vector<std::string> cats = {seed_title};
                    std::string cont;
                    for (int page = 0; page < 4; ++page) {
                        std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                                          "&list=categorymembers&cmtype=subcat&cmlimit=500&cmtitle=" +
                                          url_encode(seed_title);
                        if (!cont.empty()) api += "&cmcontinue=" + url_encode(cont);
                        auto res = http_get(api, UA, cfg.wikipedia_timeout_ms, 2'000'000);
                        if (res.status >= 400 || res.body.empty()) break;
                        try {
                            auto root = nlohmann::json::parse(res.body);
                            if (root.contains("query") && root["query"].contains("categorymembers")) {
                                for (const auto& m : root["query"]["categorymembers"]) {
                                    std::string mt = m.value("title", "");
                                    if (!mt.empty()) cats.push_back(mt);
                                }
                            }
                            cont.clear();
                            if (root.contains("continue") && root["continue"].contains("cmcontinue"))
                                cont = root["continue"].value("cmcontinue", "");
                            if (cont.empty()) break;
                        } catch (...) {
                            break;
                        }
                    }
                    for (const auto& cat : cats) {
                        cont.clear();
                        for (int page = 0; page < 4 && queue.size() < 4000; ++page) {
                            std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                                              "&list=categorymembers&cmtype=page&cmlimit=500&cmtitle=" +
                                              url_encode(cat);
                            if (!cont.empty()) api += "&cmcontinue=" + url_encode(cont);
                            auto res = http_get(api, UA, cfg.wikipedia_timeout_ms, 2'000'000);
                            if (res.status >= 400 || res.body.empty()) break;
                            try {
                                auto root = nlohmann::json::parse(res.body);
                                if (root.contains("query") && root["query"].contains("categorymembers")) {
                                    for (const auto& m : root["query"]["categorymembers"]) {
                                        std::string mt = m.value("title", "");
                                        if (mt.empty() || skip_namespace(mt)) continue;
                                        std::string url = wiki_article_url(mt);
                                        auto key2 = dedup_key(url);
                                        if (!key2 || !seen.insert(*key2).second) continue;
                                        queue.push_back({url, 1, false});
                                    }
                                }
                                cont.clear();
                                if (root.contains("continue") && root["continue"].contains("cmcontinue"))
                                    cont = root["continue"].value("cmcontinue", "");
                                if (cont.empty()) break;
                            } catch (...) {
                                break;
                            }
                        }
                    }
                    std::cerr << "Wikipedia queued " << queue.size() << " URLs from " << *title << "\n";
                }
            }
        }
        int campaign_fetches = 0;
        std::cerr << "Wikipedia BFS " << campaign.topic << " from en.wikipedia.org (have "
                  << topic_count_of(topic_counts, campaign.topic) << ", want " << per_topic << ").\n"
                  << std::flush;
        while (!queue.empty() && topic_count_of(topic_counts, campaign.topic) < per_topic &&
               campaign_fetches < max_fetches_each) {
            Item item = queue.front();
            queue.pop_front();
            auto skip_key = dedup_key(item.url);
            if (skip_key && stored_keys.count(*skip_key)) continue;
            auto item_title = wiki_title_from_url(item.url);
            if (item_title && is_hub_namespace(*item_title)) continue;
            if (stats.fetched > 0 && cfg.wikipedia_delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.wikipedia_delay_ms));
            }
            stats.fetched++;
            campaign_fetches++;
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
            std::string stored_url =
                page.final_url.empty() ? item.url : canonicalize_wiki_url(page.final_url).value_or(item.url);
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
            if (storeable && count_words(text) < WIKI_SHORT_EXTRACT_WORDS) {
                std::string want = wiki_title_from_url(stored_url).value_or(title);
                auto ext = fetch_wiki_extract(want, cfg);
                if (!ext && !iequals(want, title)) ext = fetch_wiki_extract(title, cfg);
                if (ext && count_words(ext->text) > count_words(text)) {
                    text = ext->text;
                    if (!is_blank(ext->title)) title = ext->title;
                    std::string canon = wiki_article_url(title);
                    auto canon_key = dedup_key(canon);
                    if (canon_key) {
                        stored_url = canon;
                        stored_key = canon_key;
                        seen.insert(*canon_key);
                    }
                }
            }
            bool enough_text = count_words(text) >= WIKI_MIN_ARTICLE_WORDS && !looks_like_disambiguation(title, text);
            if (storeable && enough_text) {
                bool already_have = stored_key && stored_keys.count(*stored_key);
                if (!already_have && topic_count_of(topic_counts, campaign.topic) < per_topic) {
                        WikiPage wp;
                        wp.url = stored_url;
                        wp.title = title;
                        wp.text = text;
                        wp.topic = campaign.topic;
                        auto date = published_from(html, page.last_modified);
                        wp.published_at = date.value_or("");
                        try {
                            on_page(wp);
                            stats.stored++;
                            topic_counts[campaign.topic]++;
                            if (stored_key) stored_keys.insert(*stored_key);
                            already.insert(stored_url);
                            if (stats.stored % 10 == 0 || topic_counts[campaign.topic] % 25 == 0) {
                                std::cerr << "Wikipedia stored " << stats.stored << " [" << campaign.topic << " "
                                          << topic_counts[campaign.topic] << "/" << per_topic << "] (" << title
                                          << ")\n" << std::flush;
                            }
                        } catch (const std::exception& e) {
                            stats.failed++;
                            last_error = e.what();
                            std::cerr << "Wikipedia store failed: " << last_error << "\n";
                        }
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
                auto link_title = wiki_title_from_url(link);
                if (link_title && is_hub_namespace(*link_title)) {
                    queue.push_front({link, item.depth + 1, false});
                } else {
                    queue.push_back({link, item.depth + 1, false});
                }
            }
        }
        std::cerr << "Wikipedia " << campaign.topic << " now " << topic_count_of(topic_counts, campaign.topic)
                  << " stored.\n";
    }

    if (stats.stored == 0 && already.empty()) {
        stats.error =
            "Could not reach English Wikipedia (en.wikipedia.org) from the India / Geography of India seeds. "
            "Network may be blocking Wikipedia. Last error: " +
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
        if (root.contains("error")) {
            std::cerr << "Wikipedia revision API error: " << root["error"].value("info", "unknown") << "\n";
        }
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

static std::string revision_api_url(const std::string& joined, bool first) {
    std::string api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                      "&prop=revisions&rvprop=timestamp&redirects=1";
    if (first) api += "&rvdir=newer&rvlimit=1";
    api += "&titles=" + url_encode(joined);
    return api;
}

struct RevisionFetch {
    std::map<std::string, std::string> dates;
    int status = 0;
};

static RevisionFetch fetch_revision_batch(const std::string& joined, const Config& cfg, bool first) {
    int backoff = std::max(8000, cfg.wikipedia_delay_ms);
    for (int attempt = 0; attempt < 6; ++attempt) {
        auto res = http_get(revision_api_url(joined, first), UA, cfg.wikipedia_timeout_ms, 2'000'000);
        if (res.status == 429 || res.status == 503) {
            std::cerr << "Wikipedia revision query " << res.status << ", retry in " << backoff << "ms\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
            backoff = std::min(backoff * 2, 60000);
            continue;
        }
        if (res.status >= 400 || res.body.empty()) {
            std::cerr << "Wikipedia revision query failed (" << res.status
                      << (res.error.empty() ? "" : ", " + res.error) << ")\n";
            return {{}, res.status};
        }
        return {parse_revision_query(res.body), res.status};
    }
    return {{}, 429};
}

static std::map<std::string, std::string> fetch_revisions(const std::vector<std::string>& titles,
                                                         const Config& cfg, bool first) {
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
    // First-revision params (rvdir=newer / rvlimit) are single-page only on MediaWiki.
    const size_t batch = first ? 1 : 20;
    const int pace = first ? std::max(200, cfg.wikipedia_delay_ms) : std::max(400, cfg.wikipedia_delay_ms);
    for (size_t i = 0; i < unique.size(); i += batch) {
        std::string joined;
        for (size_t j = i; j < unique.size() && j < i + batch; ++j) {
            if (!joined.empty()) joined += "|";
            joined += unique[j];
        }
        auto part = fetch_revision_batch(joined, cfg, first);
        out.insert(part.dates.begin(), part.dates.end());
        if (i + batch < unique.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pace));
        }
        if ((i / batch) % (first ? 40 : 4) == 0 || i + batch >= unique.size()) {
            std::cerr << (first ? "First" : "Last") << "-revision dates: " << out.size() << "/" << unique.size()
                      << " titles\n";
        }
    }
    return out;
}

std::map<std::string, std::string> fetch_last_revisions(const std::vector<std::string>& titles, const Config& cfg) {
    return fetch_revisions(titles, cfg, false);
}

std::map<std::string, std::string> fetch_first_revisions(const std::vector<std::string>& titles, const Config& cfg) {
    return fetch_revisions(titles, cfg, true);
}

}  // namespace kos
