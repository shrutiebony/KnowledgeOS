#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "analysis.hpp"
#include "claude.hpp"
#include "config.hpp"
#include "db.hpp"
#include "ingest.hpp"
#include "runtime/scheduler.hpp"
#include "util.hpp"

#include "httplib.h"
#include "json.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;
using kos::Config;
using kos::Store;

#include "server_http.hpp"

static std::string exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) return ".";
    return fs::path(std::string(buf, n)).parent_path().string();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return ".";
    return p.parent_path().string();
#endif
}

static std::string find_web(const Config& cfg) {
    std::vector<fs::path> cands;
    if (!cfg.web_dir.empty()) cands.push_back(cfg.web_dir);
    cands.push_back(fs::current_path() / "web");
    cands.push_back(fs::path(exe_dir()) / "web");
    cands.push_back(fs::path(exe_dir()) / "../web");
    for (const auto& p : cands) {
        if (fs::exists(p / "index.html")) return fs::weakly_canonical(p).string();
    }
    return (fs::current_path() / "web").string();
}

int main() {
    Config cfg = Config::from_env();
    Store store;
    try {
        store.open(cfg.data_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to open database: " << e.what() << "\n";
        return 1;
    }
    kos::start_analysis_runtime(store, cfg);
    std::cerr << "Analysis runtime: " << kos::analysis_runtime().worker_count() << " workers\n";
    int pruned = kos::prune_extra_datasets(store);
    if (pruned) std::cerr << "Renamed " << pruned << " leftover Wikipedia collection(s).\n";
    kos::seed_wikipedia(store, cfg);
    kos::seed_gdelt(store, cfg);

    httplib::Server svr;
    svr.set_payload_max_length(64 * 1024 * 1024);
    svr.set_read_timeout(600, 0);
    svr.set_write_timeout(600, 0);
    std::string web = find_web(cfg);
    if (!svr.set_mount_point("/", web)) {
        std::cerr << "Warning: could not mount static files from " << web << "\n";
    } else {
        std::cerr << "Serving UI from " << web << "\n";
    }

    kos::register_http_routes(svr, store, cfg);

    std::cerr << "KnowledgeOS listening on http://0.0.0.0:" << cfg.port << "\n";
    std::cerr << "Database: " << cfg.data_path << "\n";
    if (!svr.listen("0.0.0.0", cfg.port)) {
        std::cerr << "Failed to bind port " << cfg.port << "\n";
        return 1;
    }
    return 0;
}
