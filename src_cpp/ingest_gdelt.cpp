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

#include "ingest_internal.hpp"

namespace kos {

static int gdelt_topic_target(const std::string& topic, int per_topic) {
    if (iequals(topic, SHARED_TOPIC_GERMANY)) return std::min(per_topic, 1000);
    return per_topic;
}

static bool country_topics_incomplete(const std::map<std::string, int>& counts, int per_topic) {
    return topic_count_of(counts, SHARED_TOPIC_USA) < gdelt_topic_target(SHARED_TOPIC_USA, per_topic) ||
           topic_count_of(counts, SHARED_TOPIC_INDIA) < gdelt_topic_target(SHARED_TOPIC_INDIA, per_topic) ||
           topic_count_of(counts, SHARED_TOPIC_AUSTRALIA) < gdelt_topic_target(SHARED_TOPIC_AUSTRALIA, per_topic) ||
           topic_count_of(counts, SHARED_TOPIC_GERMANY) < gdelt_topic_target(SHARED_TOPIC_GERMANY, per_topic);
}

static void crawl_gdelt_and_analyze(Store& store, Config cfg, int64_t dataset_id) {
    auto dataset = store.get_dataset(dataset_id);
    if (!dataset) return;
    std::unordered_set<std::string> already;
    for (const auto& doc : store.docs_by_dataset(dataset_id, false)) {
        if (!doc.url.empty()) already.insert(doc.url);
    }
    if (!already.empty() && store.count_unscored(dataset_id) > 0) {
        std::cerr << "Deferring score of " << already.size()
                  << " already-stored GDELT pages until country harvest flushes.\n";
    }
    auto topic_counts = stored_topic_counts(store, dataset_id);
    std::cerr << "Crawling GDELT (India / United States / Australia). Already stored="
              << already.size() << " per-topic cap=" << cfg.gdelt_max_pages
              << " USA=" << topic_count_of(topic_counts, SHARED_TOPIC_USA)
              << " Germany=" << topic_count_of(topic_counts, SHARED_TOPIC_GERMANY)
              << " Australia=" << topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) << "\n";
    int persisted = 0;
    GdeltCrawlStats stats;
    try {
        stats = crawl_gdelt(cfg, already, [&](const GdeltPage& page) {
            if (!is_shared_country_topic(page.topic) && !is_shared_india_topic(page.topic)) return;
            add_document(store, *dataset, page.title, page.text, page.url, GDELT_SOURCE,
                         page.topic, page.published_at, page.published_at);
            persisted++;
            if (persisted % cfg.gdelt_flush_every == 0) {
                std::cerr << "GDELT progress: " << persisted << " new pages this run, "
                          << store.count_docs(dataset_id) << " stored. Analyzing incrementally.\n";
                try {
                    analyze_dataset(store, cfg, dataset_id, true);
                } catch (const std::exception& e) {
                    std::cerr << "GDELT incremental analysis failed: " << e.what() << "\n";
                }
            }
        }, topic_counts);
    } catch (const std::exception& e) {
        std::cerr << "GDELT crawl failed: " << e.what() << "\n";
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        return;
    }
    long stored = store.count_docs(dataset_id);
    auto ds = store.get_dataset(dataset_id);
    if (ds) {
        ds->analysis_state = stored > 0 ? "ingested" : "error";
        store.save_dataset(*ds);
    }
    if (!stats.error.empty()) std::cerr << stats.error << "\n";
    std::cerr << "GDELT crawl finished: " << stored << " pages stored (" << persisted << " new).\n";
    if (stored > 0) {
        try {
            analyze_dataset(store, cfg, dataset_id, false);
        } catch (const std::exception& e) {
            std::cerr << "GDELT analysis failed: " << e.what() << "\n";
        }
    }
}

void seed_gdelt(Store& store, const Config& cfg) {
    if (!cfg.seed_gdelt) return;
    auto existing = store.find_by_kind_and_name(KIND_GDELT, GDELT_DATASET_NAME);
    if (!existing) existing = store.find_by_name_ignore_case(GDELT_DATASET_NAME);
    if (existing) {
        if (!iequals(trim(existing->name), GDELT_DATASET_NAME) || existing->kind != KIND_GDELT) {
            existing->name = GDELT_DATASET_NAME;
            existing->kind = KIND_GDELT;
            store.save_dataset(*existing);
        }
        int per_topic = std::max(cfg.gdelt_max_pages, WIKI_MIN_COUNTRY_PAGES);
        if (!country_topics_incomplete(stored_topic_counts(store, existing->id), per_topic)) {
            std::cerr << "GDELT country topics already stored (" << store.count_docs(existing->id)
                      << " pages total).\n";
            int copied = 0;
            for (const auto& doc : store.docs_by_dataset(existing->id, false)) {
                if (!doc.created_at.empty() || doc.published_at.empty()) continue;
                copied += store.update_created_at_if_null(doc.id, doc.published_at);
            }
            if (copied) std::cerr << "Copied GDELT article dates onto createdAt for " << copied << " documents.\n";
            if (store.count_unscored(existing->id) > 0) {
                try { analyze_dataset(store, cfg, existing->id, true); } catch (...) {}
            }
            return;
        }
    }
    if (!cfg.gdelt_crawl) {
        if (existing) {
            for (const auto& doc : store.docs_by_dataset(existing->id, false)) {
                if (!doc.created_at.empty() || doc.published_at.empty()) continue;
                store.update_created_at_if_null(doc.id, doc.published_at);
            }
            try { analyze_dataset(store, cfg, existing->id, true); } catch (...) {}
        }
        return;
    }
    Dataset dataset = existing ? *existing : create_dataset(store, GDELT_DATASET_NAME, KIND_GDELT);
    dataset.analysis_state = "crawling";
    store.save_dataset(dataset);
    std::thread([store_ptr = &store, cfg, id = dataset.id]() {
        try {
            crawl_gdelt_and_analyze(*store_ptr, cfg, id);
        } catch (const std::exception& e) {
            std::cerr << "GDELT crawl thread failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "GDELT crawl thread failed with an unknown error.\n";
        }
    }).detach();
    std::cerr << "Started GDELT crawl in the background (maxPages=" << cfg.gdelt_max_pages
              << ", depth=" << cfg.gdelt_max_depth << ").\n";
}


}  // namespace kos
