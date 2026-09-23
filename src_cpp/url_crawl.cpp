#include "url_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "util.hpp"

#include <chrono>
#include <deque>
#include <thread>
#include <unordered_set>

namespace kos {

static const char* UA = "KnowledgeOS/1.0 (local collection analysis)";

std::vector<std::string> extract_same_host_links(const std::string& page_url, const std::string& html) {
    auto base = normalize_url(page_url);
    if (!base) return {};
    auto parsed = parse_html(html);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& href : parsed.hrefs) {
        auto n = resolve_url(*base, href);
        if (!n || *n == *base || !same_host(*base, *n) || skippable_url(*n)) continue;
        if (seen.insert(*n).second) out.push_back(*n);
    }
    return out;
}

UrlCrawlResult crawl_urls(const std::vector<std::string>& seed_urls, const Config& cfg) {
    struct Item {
        std::string url;
        int depth;
        bool seed;
        std::string seed_url;
    };
    std::deque<Item> queue;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> seed_keys;
    UrlCrawlResult result;
    int fetches = 0;

    for (const auto& raw : seed_urls) {
        if (is_blank(raw)) continue;
        auto n = normalize_url(raw);
        auto key = n ? dedup_key(*n) : std::nullopt;
        if (!key || !seen.insert(*key).second) continue;
        seed_keys.insert(*key);
        queue.push_back({*n, 0, true, *n});
        result.seed_count++;
    }

    while (!queue.empty() && fetches < cfg.crawl_max_pages) {
        Item item = queue.front();
        queue.pop_front();
        if (fetches > 0 && cfg.crawl_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.crawl_delay_ms));
        }
        fetches++;
        auto fetched = http_get(item.url, UA, cfg.crawl_timeout_ms, 1'500'000);
        if (fetched.status >= 400 || fetched.body.empty() || !fetched.error.empty()) {
            result.failed_pages++;
            continue;
        }
        std::string ctype = ascii_lower(fetched.content_type);
        if (!ctype.empty() &&
            (ctype.find("image/") != std::string::npos || ctype.find("audio/") != std::string::npos ||
             ctype.find("video/") != std::string::npos || ctype.find("application/pdf") != std::string::npos ||
             ctype.find("application/zip") != std::string::npos || ctype.find("octet-stream") != std::string::npos ||
             ctype.find("font/") != std::string::npos)) {
            result.failed_pages++;
            continue;
        }
        std::string stored = fetched.final_url.empty() ? item.url : (normalize_url(fetched.final_url).value_or(item.url));
        auto stored_key = dedup_key(stored);
        if (stored_key) {
            seen.insert(*stored_key);
            if (item.seed) seed_keys.insert(*stored_key);
            else if (!same_host(item.url, stored)) {
                result.failed_pages++;
                continue;
            }
        }
        auto html = parse_html(fetched.body);
        std::string text = html.body_text;
        std::string title = html.title;
        if (!is_blank(text)) {
            CrawledPage p;
            p.url = stored;
            p.title = is_blank(title) ? stored : title;
            p.text = text;
            p.host = host_of(stored).value_or("");
            p.seed_url = item.seed_url.empty() ? stored : item.seed_url;
            result.pages.push_back(p);
        } else if (item.seed) {
            result.failed_pages++;
        }
        if (item.depth >= cfg.crawl_max_depth) continue;
        for (const auto& link : extract_same_host_links(stored, fetched.body)) {
            auto key = dedup_key(link);
            if (!key || !seen.insert(*key).second) continue;
            if (!same_host(stored, link)) {
                seen.erase(*key);
                continue;
            }
            queue.push_back({link, item.depth + 1, false, item.seed_url});
        }
    }
    for (const auto& page : result.pages) {
        auto k = dedup_key(page.url);
        if (!k || !seed_keys.count(*k)) result.extra_pages++;
    }
    return result;
}

}  // namespace kos
