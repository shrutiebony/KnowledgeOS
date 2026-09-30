#include "wiki_crawl.hpp"
#include "wiki_detail.hpp"
#include "http_client.hpp"
#include "util.hpp"

#include "json.hpp"

#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <unordered_set>
#include <vector>

namespace kos {

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
        auto res = http_get(revision_api_url(joined, first), WIKI_HTTP_UA, cfg.wikipedia_timeout_ms, 2'000'000);
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
