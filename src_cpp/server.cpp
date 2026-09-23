#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "analysis.hpp"
#include "config.hpp"
#include "db.hpp"
#include "ingest.hpp"
#include "util.hpp"

#include "httplib.h"
#include "json.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;
using kos::Config;
using kos::Store;

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

static void send_json(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

static void send_error(httplib::Response& res, const std::string& msg, int status) {
    send_json(res, json{{"error", msg}}, status);
}

static int64_t path_id(const httplib::Request& req, const char* key) {
    return std::stoll(req.path_params.at(key));
}

static std::vector<int64_t> parse_ids(const httplib::Request& req) {
    std::vector<int64_t> out;
    if (!req.has_param("ids")) return out;
    for (const auto& part : kos::split_csv(req.get_param_value("ids"))) {
        try {
            out.push_back(std::stoll(part));
        } catch (...) {
        }
    }
    return out;
}

static std::string param(const httplib::Request& req, const char* key, const std::string& fallback = "") {
    return req.has_param(key) ? req.get_param_value(key) : fallback;
}

static json list_datasets(Store& store) {
    auto all = store.all_datasets();
    std::vector<kos::Dataset> wiki, user;
    for (const auto& d : all) {
        if (d.kind == kos::KIND_WIKI) wiki.push_back(d);
        else if (d.kind == kos::KIND_WIKI_SUBSET) continue;
        else if (d.kind == kos::KIND_UPLOAD || d.kind == kos::KIND_URLS) user.push_back(d);
    }
    json out = json::array();
    for (const auto& d : wiki) out.push_back(kos::dataset_brief(store, d));
    std::unordered_set<std::string> seen;
    for (const auto& d : user) {
        std::string key = kos::ascii_lower(d.name);
        if (!seen.insert(key).second) continue;
        out.push_back(kos::dataset_brief(store, d));
    }
    return out;
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
    int pruned = kos::prune_extra_datasets(store);
    if (pruned) std::cerr << "Pruned " << pruned << " leftover collection(s).\n";
    kos::seed_wikipedia_india(store, cfg);

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

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, json{{"status", "ok"}});
    });

    svr.Get("/datasets", [&](const httplib::Request&, httplib::Response& res) {
        try {
            send_json(res, list_datasets(store));
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Post("/datasets/upload", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string name = param(req, "name");
            if (name.empty() && req.has_file("name")) {
                name = req.get_file_value("name").content;
            }
            std::vector<kos::UploadedFile> files;
            for (const auto& [field, file] : req.files) {
                if (field != "files" && field != "file") continue;
                kos::UploadedFile u;
                u.filename = file.filename;
                u.content_type = file.content_type;
                u.bytes = file.content;
                files.push_back(std::move(u));
            }
            auto dataset = kos::ingest_uploads(store, name, files);
            kos::analyze_dataset(store, cfg, dataset.id, false);
            auto fresh = store.get_dataset(dataset.id).value_or(dataset);
            send_json(res, kos::dataset_brief(store, fresh));
        } catch (const std::invalid_argument& e) {
            send_error(res, e.what(), 400);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Post("/datasets/from-urls", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body.empty() ? "{}" : req.body);
            std::string name = body.value("name", "");
            std::vector<std::string> urls;
            if (body.contains("urls") && body["urls"].is_array()) {
                for (const auto& u : body["urls"]) {
                    if (u.is_string()) urls.push_back(u.get<std::string>());
                }
            }
            auto ingested = kos::ingest_urls(store, cfg, name, urls);
            kos::analyze_dataset(store, cfg, ingested.dataset.id, false);
            auto fresh = store.get_dataset(ingested.dataset.id).value_or(ingested.dataset);
            json out = kos::dataset_brief(store, fresh);
            out["seedCount"] = ingested.seed_count;
            out["extraPages"] = ingested.extra_pages;
            out["ingestedPages"] = ingested.ingested_pages;
            out["failedPages"] = ingested.failed_pages;
            out["crawlMaxDepth"] = ingested.max_depth;
            out["crawlMaxPages"] = ingested.max_pages;
            send_json(res, out);
        } catch (const std::invalid_argument& e) {
            send_error(res, e.what(), 400);
        } catch (const json::exception& e) {
            send_error(res, e.what(), 400);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    auto need_ds = [&](int64_t id) {
        auto ds = store.get_dataset(id);
        if (!ds) throw std::runtime_error("dataset not found");
        return *ds;
    };

    svr.Post("/datasets/:id/analyze", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            kos::analyze_dataset(store, cfg, id, false);
            send_json(res, kos::dataset_brief(store, *store.get_dataset(id)));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/summary", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            send_json(res, kos::summary_json(store, id, param(req, "metric", "documents"), param(req, "topic"),
                                            parse_ids(req)));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/graph", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            send_json(res, kos::graph_json(store, id, param(req, "topic"), parse_ids(req)));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/examples", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            std::string band = param(req, "band");
            int limit = 6;
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
            }
            send_json(res, kos::examples_json(store, id, band, limit, param(req, "topic"), parse_ids(req)));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/breakdown", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            std::string by = param(req, "by", "source");
            auto rows = kos::breakdown_rows(store, id, by, param(req, "topic"), parse_ids(req));
            send_json(res, json{{"by", by}, {"available", !rows.empty()}, {"rows", rows}});
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/documents/:docId", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            int64_t doc_id = path_id(req, "docId");
            need_ds(id);
            send_json(res, kos::explanation_json(store, id, doc_id));
        } catch (const std::invalid_argument& e) {
            send_error(res, e.what(), 400);
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Get("/datasets/:id/topics", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            send_json(res, store.distinct_topics(id));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    svr.Post("/datasets/:id/graphsage/train", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t id = path_id(req, "id");
            need_ds(id);
            std::string labels;
            if (!req.body.empty()) {
                try {
                    auto body = json::parse(req.body);
                    labels = body.value("labelsPath", "");
                } catch (...) {
                }
            }
            send_json(res, kos::train_graphsage(store, id, labels));
        } catch (const std::runtime_error& e) {
            send_error(res, e.what(), 404);
        } catch (const std::exception& e) {
            send_error(res, e.what(), 500);
        }
    });

    std::cerr << "KnowledgeOS listening on http://0.0.0.0:" << cfg.port << "\n";
    std::cerr << "Database: " << cfg.data_path << "\n";
    if (!svr.listen("0.0.0.0", cfg.port)) {
        std::cerr << "Failed to bind port " << cfg.port << "\n";
        return 1;
    }
    return 0;
}
