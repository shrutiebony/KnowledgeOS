#include "url_crawl.hpp"
#include "html.hpp"
#include "http_client.hpp"
#include "models.hpp"
#include "util.hpp"

#include <chrono>
#include <deque>
#include <thread>
#include <unordered_set>

namespace kos {

static const char* UA = "KnowledgeOS/1.0 (local collection analysis)";

bool looks_like_html_page(const std::string& url) {
    if (skippable_url(url)) return false;
    auto path = url_path(url);
    if (!path) return false;
    std::string lower = ascii_lower(*path);
    if (lower.rfind("/wiki/", 0) == 0) return true;
    return true;
}

std::vector<std::string> extract_same_host_links(const std::string& page_url, const std::string& html) {
    auto base = normalize_url(page_url);
    if (!base) return {};
    auto parsed = parse_html(html);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& href : parsed.hrefs) {
        auto n = resolve_url(*base, href);
        if (!n || *n == *base || !same_host(*base, *n) || !looks_like_html_page(*n)) continue;
        if (seen.insert(*n).second) out.push_back(*n);
    }
    return out;
}

UrlReachability probe_same_host_reachability(const std::vector<std::string>& seed_urls, const Config& cfg) {
    UrlReachability result;
    struct Item {
        std::string url;
        std::string site;
    };
    std::deque<Item> queue;
    std::unordered_set<std::string> discovered;
    int min_pages = std::max(1, cfg.url_probe_min_pages);
    auto started = std::chrono::steady_clock::now();

    for (const auto& raw : seed_urls) {
        if (is_blank(raw)) continue;
        if (!is_public_http_url(raw)) continue;
        auto n = normalize_url(raw);
        auto key = n ? dedup_key(*n) : std::nullopt;
        if (!key || !discovered.insert(*key).second) continue;
        queue.push_back({*n, *n});
    }
    if (discovered.empty()) {
        result.error = URL_MIN_PAGES_ERROR;
        return result;
    }
    if (static_cast<int>(discovered.size()) >= min_pages) {
        result.reachable = static_cast<int>(discovered.size());
        result.ok = true;
        return result;
    }

    while (!queue.empty() && static_cast<int>(discovered.size()) < min_pages &&
           result.fetched < cfg.url_probe_max_fetches) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
        if (elapsed >= cfg.url_probe_timeout_ms) break;
        Item item = queue.front();
        queue.pop_front();
        if (result.fetched > 0 && cfg.url_probe_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.url_probe_delay_ms));
        }
        result.fetched++;
        auto fetched = http_get(item.url, UA, cfg.url_probe_page_timeout_ms, 800'000);
        if (fetched.status >= 400 || fetched.body.empty() || !fetched.error.empty()) continue;
        std::string ctype = ascii_lower(fetched.content_type);
        if (!ctype.empty() &&
            (ctype.find("image/") != std::string::npos || ctype.find("audio/") != std::string::npos ||
             ctype.find("video/") != std::string::npos || ctype.find("application/pdf") != std::string::npos ||
             ctype.find("application/zip") != std::string::npos || ctype.find("octet-stream") != std::string::npos ||
             ctype.find("font/") != std::string::npos)) {
            continue;
        }
        std::string stored = fetched.final_url.empty() ? item.url : (normalize_url(fetched.final_url).value_or(item.url));
        if (!is_public_http_url(stored)) continue;
        if (!same_host(item.site, stored) && !same_host(item.url, stored)) continue;
        std::string site = same_host(item.site, stored) ? item.site : stored;
        auto stored_key = dedup_key(stored);
        if (stored_key) discovered.insert(*stored_key);
        for (const auto& link : extract_same_host_links(stored, fetched.body)) {
            if (!is_public_http_url(link) || !same_host(site, link) || !looks_like_html_page(link)) continue;
            auto key = dedup_key(link);
            if (!key || !discovered.insert(*key).second) continue;
            if (static_cast<int>(discovered.size()) >= min_pages) break;
            queue.push_back({link, site});
        }
    }

    result.reachable = static_cast<int>(discovered.size());
    result.ok = result.reachable >= min_pages;
    if (!result.ok) result.error = URL_MIN_PAGES_ERROR;
    return result;
}

UrlCrawlResult crawl_urls(const std::vector<std::string>& seed_urls, const Config& cfg,
                          const std::function<void(const CrawledPage&)>& on_page) {
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
            if (on_page) on_page(p);
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
