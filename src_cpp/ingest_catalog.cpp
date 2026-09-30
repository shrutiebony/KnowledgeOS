#include "ingest.hpp"
#include "analysis.hpp"
#include "gdelt_crawl.hpp"
#include "html.hpp"
#include "pdf.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include "miniz.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace kos {

void delete_dataset_and_contents(Store& store, const Dataset& dataset) {
    // delete_dataset now drops documents, edges, child rows, and orphans in one transaction.
    store.delete_dataset(dataset.id);
}

bool is_protected_wikipedia(const Dataset& dataset) {
    if (dataset.kind == KIND_WIKI || dataset.kind == KIND_WIKI_SUBSET) return true;
    return is_wikipedia_name(dataset.name);
}

bool is_gdelt_dataset(const Dataset& dataset) {
    if (dataset.kind == KIND_GDELT) return true;
    return ascii_lower(trim(dataset.name)) == "gdelt";
}

int visible_dataset_count(Store& store) {
    int n = 0;
    for (const auto& d : store.all_datasets()) {
        if (d.kind == KIND_WIKI_SUBSET) continue;
        n++;
    }
    return n;
}

bool at_dataset_cap(Store& store) {
    return visible_dataset_count(store) >= MAX_DATASETS;
}

int prune_extra_datasets(Store& store) {
    int renamed = 0;
    for (const auto& d : store.all_datasets()) {
        if (!is_protected_wikipedia(d)) continue;
        if (d.kind == KIND_WIKI_SUBSET) continue;
        if (iequals(trim(d.name), WIKI_DATASET_NAME) && d.kind == KIND_WIKI) continue;
        Dataset next = d;
        next.name = WIKI_DATASET_NAME;
        next.kind = KIND_WIKI;
        store.save_dataset(next);
        renamed++;
        std::cerr << "Renamed leftover Wikipedia collection '" << d.name << "' to " << WIKI_DATASET_NAME << "\n";
    }
    return renamed;
}

nlohmann::json dataset_brief(Store& store, const Dataset& dataset) {
    nlohmann::json m;
    m["id"] = dataset.id;
    m["name"] = dataset.name;
    m["kind"] = dataset.kind;
    m["analysisState"] = dataset.analysis_state;
    m["documentCount"] = store.count_docs(dataset.id);
    m["topics"] = nlohmann::json::array();
    m["graphSageStatus"] = dataset.graph_sage_status;
    m["wikipedia"] = is_protected_wikipedia(dataset);
    m["gdelt"] = is_gdelt_dataset(dataset);
    m["deletable"] = !is_protected_wikipedia(dataset) && !is_gdelt_dataset(dataset);
    return m;
}

}  // namespace kos
