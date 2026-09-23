#pragma once

#include "config.hpp"
#include "db.hpp"
#include "models.hpp"
#include "url_crawl.hpp"

#include "json.hpp"

#include <string>
#include <vector>

namespace kos {

struct UploadedFile {
    std::string filename;
    std::string content_type;
    std::string bytes;
};

struct UrlIngestResult {
    Dataset dataset;
    int seed_count = 0;
    int extra_pages = 0;
    int ingested_pages = 0;
    int failed_pages = 0;
    int max_depth = 0;
    int max_pages = 0;
};

Dataset create_dataset(Store& store, const std::string& name, const std::string& kind);
Document add_document(Store& store, const Dataset& dataset, std::string title, std::string text,
                      const std::string& url, const std::string& source, const std::string& topic,
                      const std::string& published_at, const std::string& created_at = "");
Dataset ingest_uploads(Store& store, const std::string& name, const std::vector<UploadedFile>& files);
UrlIngestResult ingest_urls(Store& store, const Config& cfg, const std::string& name,
                            const std::vector<std::string>& urls);
void delete_dataset_and_contents(Store& store, const Dataset& dataset);
bool is_protected_wikipedia(const Dataset& dataset);
bool is_gdelt_dataset(const Dataset& dataset);
int visible_dataset_count(Store& store);
bool at_dataset_cap(Store& store);
int prune_extra_datasets(Store& store);
void seed_wikipedia(Store& store, const Config& cfg);
void seed_gdelt(Store& store, const Config& cfg);
inline void seed_wikipedia_india(Store& store, const Config& cfg) { seed_wikipedia(store, cfg); }
nlohmann::json dataset_brief(Store& store, const Dataset& dataset);

}  // namespace kos
