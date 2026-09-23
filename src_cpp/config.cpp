#include "config.hpp"

#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace kos {

static const char* first_env(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (!name) continue;
        const char* v = std::getenv(name);
        if (v && *v) return v;
    }
    return nullptr;
}

bool env_flag(const char* name, bool fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    std::string s;
    for (const char* p = v; *p; ++p) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return fallback;
}

int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

double env_double(const char* name, double fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stod(v);
    } catch (...) {
        return fallback;
    }
}

std::string env_str(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    return v;
}

Config Config::from_env() {
    Config c;
    const char* port = first_env({"PORT", "SERVER_PORT"});
    if (port) {
        try { c.port = std::stoi(port); } catch (...) {}
    }
    c.data_path = env_str("KNOWLEDGEOS_DB", env_str("KNOWLEDGEOS_DATA", "./data/knowledgeos.db"));
    c.web_dir = env_str("KNOWLEDGEOS_WEB", "");
    c.embed_dim = env_int("KNOWLEDGEOS_EMBED_DIM", 128);
    c.embed_url = env_str("KNOWLEDGEOS_EMBED_URL", "");
    c.graph_k = env_int("KNOWLEDGEOS_GRAPH_K", 8);
    c.graph_min_cosine = env_double("KNOWLEDGEOS_GRAPH_MIN_COSINE", 0.32);
    c.seed_wikipedia = env_flag("KNOWLEDGEOS_SEED_WIKIPEDIA", true);
    bool crawl = env_flag("KNOWLEDGEOS_WIKIPEDIA_INDIA_CRAWL", true);
    if (std::getenv("CRAWL") && *std::getenv("CRAWL")) {
        crawl = env_flag("CRAWL", crawl);
    }
    c.wikipedia_crawl = crawl;
    c.wikipedia_max_pages = env_int("KNOWLEDGEOS_WIKIPEDIA_INDIA_MAX_PAGES", 2000);
    c.wikipedia_max_depth = env_int("KNOWLEDGEOS_WIKIPEDIA_INDIA_MAX_DEPTH", 4);
    c.wikipedia_delay_ms = env_int("KNOWLEDGEOS_WIKIPEDIA_INDIA_DELAY_MS", 300);
    c.wikipedia_timeout_ms = env_int("KNOWLEDGEOS_WIKIPEDIA_INDIA_TIMEOUT_MS", 15000);
    c.wikipedia_flush_every = env_int("KNOWLEDGEOS_WIKIPEDIA_INDIA_FLUSH_EVERY", 25);
    c.crawl_max_depth = env_int("KNOWLEDGEOS_CRAWL_MAX_DEPTH", 2);
    c.crawl_max_pages = env_int("KNOWLEDGEOS_CRAWL_MAX_PAGES", 80);
    c.crawl_timeout_ms = env_int("KNOWLEDGEOS_CRAWL_TIMEOUT_MS", 12000);
    c.crawl_delay_ms = env_int("KNOWLEDGEOS_CRAWL_DELAY_MS", 200);
    c.band_ai_min = env_double("KNOWLEDGEOS_BAND_AI_MIN", 0.58);
    c.band_human_max = env_double("KNOWLEDGEOS_BAND_HUMAN_MAX", 0.42);
    c.band_max_interval = env_double("KNOWLEDGEOS_BAND_MAX_INTERVAL", 0.50);
    c.rank_mix = env_double("KNOWLEDGEOS_CALIBRATE_RANK_MIX", 0.38);
    return c;
}

}  // namespace kos
