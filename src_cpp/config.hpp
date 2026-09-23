#pragma once

#include <string>

namespace kos {

struct Config {
    int port = 8080;
    std::string data_path = "./data/knowledgeos.db";
    std::string web_dir;
    int embed_dim = 128;
    std::string embed_url;
    int graph_k = 8;
    double graph_min_cosine = 0.32;
    bool seed_wikipedia = true;
    bool wikipedia_crawl = true;
    int wikipedia_max_pages = 2000;
    int wikipedia_max_depth = 4;
    int wikipedia_delay_ms = 300;
    int wikipedia_timeout_ms = 15000;
    int wikipedia_flush_every = 25;
    int crawl_max_depth = 2;
    int crawl_max_pages = 80;
    int crawl_timeout_ms = 12000;
    int crawl_delay_ms = 200;
    double band_ai_min = 0.58;
    double band_human_max = 0.42;
    double band_max_interval = 0.50;
    double rank_mix = 0.38;

    static Config from_env();
};

bool env_flag(const char* name, bool fallback);
int env_int(const char* name, int fallback);
double env_double(const char* name, double fallback);
std::string env_str(const char* name, const std::string& fallback);

}  // namespace kos
