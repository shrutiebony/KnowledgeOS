#pragma once

#include "config.hpp"

#include <string>
#include <vector>

namespace kos {

struct CrawledPage {
    std::string url;
    std::string title;
    std::string text;
    std::string host;
    std::string seed_url;
};

struct UrlCrawlResult {
    std::vector<CrawledPage> pages;
    int seed_count = 0;
    int extra_pages = 0;
    int failed_pages = 0;
};

UrlCrawlResult crawl_urls(const std::vector<std::string>& seed_urls, const Config& cfg);
std::vector<std::string> extract_same_host_links(const std::string& page_url, const std::string& html);

}  // namespace kos
