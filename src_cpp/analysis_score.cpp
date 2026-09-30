#include "analysis.hpp"
#include "gdelt_crawl.hpp"
#include "runtime/scheduler.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "analysis_detail.hpp"

namespace kos {

using nlohmann::json;

std::vector<double> ranks01(const std::vector<double>& values) {
    int n = static_cast<int>(values.size());
    std::vector<double> ranks(n, 0);
    if (n == 0) return ranks;
    if (n == 1) {
        ranks[0] = 0.5;
        return ranks;
    }
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return values[a] < values[b]; });
    for (int start = 0; start < n;) {
        int end = start;
        while (end + 1 < n && values[idx[end + 1]] == values[idx[start]]) end++;
        double avg = (start + end) / 2.0 / (n - 1.0);
        for (int k = start; k <= end; ++k) ranks[idx[k]] = avg;
        start = end + 1;
    }
    return ranks;
}

struct Stats {
    double mean = 0;
    double std = 1;
};

static Stats stats_of(const std::vector<double>& values) {
    if (values.empty()) return {};
    double mean = 0;
    for (double v : values) mean += v;
    mean /= static_cast<double>(values.size());
    double var = 0;
    for (double v : values) var += (v - mean) * (v - mean);
    double stdv = values.size() < 2 ? 1 : std::sqrt(var / static_cast<double>(values.size() - 1));
    if (stdv < 1e-6) stdv = 1;
    return {mean, stdv};
}

static double zscore(double v, const Stats& s) { return (v - s.mean) / s.std; }


static std::string signals_json(const std::map<std::string, double>& signals) {
    std::ostringstream sb;
    sb << "{";
    bool first = true;
    for (const auto& [k, v] : signals) {
        if (!first) sb << ",";
        first = false;
        sb << "\"" << k << "\":" << round3(v);
    }
    sb << "}";
    return sb.str();
}





int doc_year(const Document& d) { return year_of_date(doc_date_of(d)); }

bool is_pre_2019(const Document& d) {
    int y = doc_year(d);
    return y > 0 && y < ERA_SPLIT_YEAR;
}

bool is_post_2019(const Document& d) { return doc_year(d) >= ERA_SPLIT_YEAR; }

static double excess_vs(double v, const Stats& s) {
    return clamp01(0.5 + 0.5 * std::tanh(zscore(v, s) / 2.0));
}

static double raw_stock_of(const Document& d) {
    if (d.explanation_json.find("stock_phrase_raw") != std::string::npos) {
        return parse_signal(d.explanation_json, "stock_phrase_raw");
    }
    return parse_signal(d.explanation_json, "stock_phrase_detector");
}

static void force_pre2019_human(Document& doc, double style) {
    double p = clamp01(0.025 + 0.04 * clamp01(style));
    doc.p_ai = p;
    doc.ci_low = clamp01(p - 0.03);
    doc.ci_high = clamp01(p + 0.08);
    doc.band = BAND_HUMAN;
}

// Score post-2019 pages against the pre-2019 centroid / rates in this document set.
// Pre-2019 pages are the human baseline: p_ai near 0, likely human.
// allow_keep_stored: view-time path keeps persisted scores when the visible set has no pre-2019 baseline.
void apply_era_scores(std::vector<Document>& docs, const Config& cfg, bool allow_keep_stored) {
    if (docs.empty()) return;

    std::vector<std::vector<float>> pre_vecs;
    std::vector<double> pre_style, pre_stock, pre_ngram, pre_uni, pre_ttr, pre_burst;
    for (const auto& d : docs) {
        if (!is_pre_2019(d)) continue;
        if (!d.embedding.empty()) pre_vecs.push_back(d.embedding);
        pre_style.push_back(nz(d.detector_stylometry));
        pre_stock.push_back(raw_stock_of(d));
        pre_ngram.push_back(nz(d.detector_repetition));
        pre_uni.push_back(nz(d.detector_uniformity));
        pre_ttr.push_back(nz(d.type_token_ratio));
        pre_burst.push_back(nz(d.burstiness));
    }
    const bool have_pre = !pre_vecs.empty();
    if (!have_pre && allow_keep_stored) {
        for (auto& d : docs) {
            if (is_pre_2019(d)) force_pre2019_human(d, nz(d.detector_stylometry));
        }
        return;
    }

    std::vector<std::vector<float>> fallback;
    if (!have_pre) {
        std::vector<int> order(docs.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return nz(docs[a].detector_stylometry) < nz(docs[b].detector_stylometry);
        });
        int baseline_n = std::max(2, std::min(8, static_cast<int>(docs.size()) / 6));
        for (int i = 0; i < std::min(baseline_n, static_cast<int>(order.size())); ++i) {
            if (!docs[order[i]].embedding.empty()) fallback.push_back(docs[order[i]].embedding);
        }
        if (fallback.empty()) {
            for (const auto& d : docs) {
                if (!d.embedding.empty()) fallback.push_back(d.embedding);
            }
        }
    }
    auto cent = centroid(have_pre ? pre_vecs : fallback);

    std::vector<double> pre_dist;
    if (have_pre) {
        for (const auto& v : pre_vecs) pre_dist.push_back(1.0 - cosine(v, cent));
    }
    Stats style_s = stats_of(pre_style);
    Stats stock_s = stats_of(pre_stock);
    Stats ngram_s = stats_of(pre_ngram);
    Stats uni_s = stats_of(pre_uni);
    Stats dist_s = stats_of(pre_dist);

    std::vector<double> all_ttr, all_burst;
    for (const auto& d : docs) {
        all_ttr.push_back(nz(d.type_token_ratio));
        all_burst.push_back(nz(d.burstiness));
    }
    Stats ttr_stats = have_pre ? stats_of(pre_ttr) : stats_of(all_ttr);
    Stats burst_stats = have_pre ? stats_of(pre_burst) : stats_of(all_burst);

    std::vector<double> raw_anomaly(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) {
        raw_anomaly[i] = docs[i].embedding.empty() || cent.empty() ? 0.5
                                                                    : (1.0 - cosine(docs[i].embedding, cent));
    }
    auto anomaly_rank = ranks01(raw_anomaly);

    std::vector<Estimate> raw_estimates(docs.size());
    std::vector<char> pre_flag(docs.size(), 0);
    std::vector<double> style_raw(docs.size(), 0);
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        const bool pre = is_pre_2019(doc);
        pre_flag[i] = pre ? 1 : 0;
        double style = nz(doc.detector_stylometry);
        double stock = raw_stock_of(doc);
        double ngram = nz(doc.detector_repetition);
        double uni = nz(doc.detector_uniformity);
        style_raw[i] = style;

        double anomaly = have_pre
            ? clamp01(0.45 * excess_vs(raw_anomaly[i], dist_s) + 0.55 * anomaly_rank[i])
            : clamp01(0.45 * clamp01(raw_anomaly[i] * 1.4) + 0.55 * anomaly_rank[i]);
        double style_in = have_pre ? excess_vs(style, style_s) : style;
        double stock_in = have_pre ? excess_vs(stock, stock_s) : stock;
        double ngram_in = have_pre ? excess_vs(ngram, ngram_s) : ngram;
        double uni_in = have_pre ? excess_vs(uni, uni_s) : uni;
        double zt = zscore(nz(doc.type_token_ratio), ttr_stats);
        double zb = zscore(nz(doc.burstiness), burst_stats);
        double deviation = clamp01((std::abs(zt) + std::abs(zb)) / 6.0);
        double recency = pre ? 0.0 : post_chatgpt_signal(doc_date_of(doc));

        std::map<std::string, double> raw;
        raw["stylometry"] = style_in;
        raw["stock_phrase_detector"] = stock_in;
        raw["stock_phrase_raw"] = stock;
        raw["sentence_uniformity_detector"] = uni_in;
        raw["ngram_repetition_detector"] = ngram_in;
        raw["embedding_anomaly"] = anomaly;
        raw["stylometry_deviation"] = deviation;
        raw["post_chatgpt"] = recency;

        raw_estimates[i] = calibrate(raw, cfg);
        doc.embedding_anomaly = anomaly;
        doc.stylometry_deviation = deviation;
    }

    std::vector<double> post_p;
    std::vector<size_t> post_i;
    for (size_t i = 0; i < docs.size(); ++i) {
        if (pre_flag[i]) continue;
        post_i.push_back(i);
        post_p.push_back(raw_estimates[i].p_ai);
    }
    auto post_rank = ranks01(post_p);
    for (size_t k = 0; k < post_i.size(); ++k) {
        size_t i = post_i[k];
        auto mixed = mix_with_rank(raw_estimates[i], post_rank[k], cfg);
        docs[i].p_ai = mixed.p_ai;
        docs[i].ci_low = mixed.ci_low;
        docs[i].ci_high = mixed.ci_high;
        docs[i].band = mixed.band;
        docs[i].explanation_json = signals_json(mixed.signals);
    }
    for (size_t i = 0; i < docs.size(); ++i) {
        if (!pre_flag[i]) continue;
        auto sigs = raw_estimates[i].signals;
        sigs["post_chatgpt"] = 0;
        docs[i].explanation_json = signals_json(sigs);
        force_pre2019_human(docs[i], style_raw[i]);
    }
}

static void assign_scores(std::vector<Document>& docs, const Config& cfg) {
    for (auto& doc : docs) fill_local_signals(doc, cfg);
    apply_era_scores(docs, cfg, false);
}

void fill_stylometry_only(Document& doc) {
    Features f = stylometry_analyze(doc.text);
    doc.word_count = f.word_count;
    doc.type_token_ratio = f.type_token_ratio;
    doc.avg_sentence_length = f.avg_sentence_length;
    doc.sentence_length_std = f.sentence_length_std;
    doc.burstiness = f.burstiness;
    doc.punctuation_ratio = f.punctuation_ratio;
    doc.char_entropy = f.char_entropy;
    doc.repetition_score = f.repetition_score;
    doc.detector_stylometry = stylometry_ai_score(f);
    double stock = 0;
    for (const auto& det : run_detectors(doc.text, f)) {
        if (det.name == "stock_phrase_detector") stock = det.score;
        else if (det.name == "ngram_repetition_detector") doc.detector_repetition = det.score;
        else if (det.name == "sentence_uniformity_detector") doc.detector_uniformity = det.score;
    }
    doc.explanation_json = signals_json({{"stock_phrase_raw", stock}, {"stock_phrase_detector", stock}});
}

void fill_embedding_only(Document& doc, const Config& cfg) {
    doc.embedding = hashed_embed(doc.title + "\n" + doc.text, cfg.embed_dim);
}

void fill_local_signals(Document& doc, const Config& cfg) {
    fill_stylometry_only(doc);
    fill_embedding_only(doc, cfg);
}

void calibrate_collection(std::vector<Document>& docs, const Config& cfg) {
    apply_era_scores(docs, cfg, false);
}


static void rebuild_graph(Store& store, int64_t dataset_id, const std::vector<Document>& docs, const Config& cfg) {
    store.delete_edges(dataset_id);
    store.insert_edges(knn_edges(docs, cfg.graph_k, cfg.graph_min_cosine, dataset_id));
}

void rebuild_knn_graph(Store& store, int64_t dataset_id, const std::vector<Document>& docs, const Config& cfg) {
    rebuild_graph(store, dataset_id, docs, cfg);
}

static std::vector<float> gs_features(const Document& doc) {
    return {
        static_cast<float>(nz(doc.detector_stylometry)),
        static_cast<float>(nz(doc.detector_repetition)),
        static_cast<float>(nz(doc.detector_uniformity)),
        static_cast<float>(nz(doc.embedding_anomaly)),
        static_cast<float>(nz(doc.stylometry_deviation)),
        static_cast<float>(nz(doc.type_token_ratio)),
        static_cast<float>(nz(doc.burstiness)),
        static_cast<float>(nz(doc.p_ai))
    };
}

void mean_aggregate_graphsage(Store& store, Dataset& dataset) {
    auto docs = store.docs_by_dataset(dataset.id, true);
    if (docs.size() < 2) {
        dataset.graph_sage_status = GS_NOT_TRAINED;
        dataset.graph_sage_message = "Need at least two documents to build GraphSAGE representations.";
        store.save_dataset(dataset);
        return;
    }
    auto edges = store.edges_by_dataset(dataset.id);
    std::unordered_map<int64_t, const Document*> by_id;
    for (const auto& d : docs) by_id[d.id] = &d;
    for (auto& doc : docs) {
        auto self = gs_features(doc);
        std::vector<float> acc = self;
        int n = 1;
        for (const auto& e : edges) {
            if (e.source_document_id != doc.id) continue;
            auto it = by_id.find(e.target_document_id);
            if (it == by_id.end()) continue;
            auto f = gs_features(*it->second);
            for (size_t i = 0; i < acc.size() && i < f.size(); ++i) acc[i] += f[i];
            n++;
        }
        float inv = 1.0f / static_cast<float>(n);
        for (float& v : acc) v *= inv;
        doc.gnn_embedding = acc;
    }
    store.save_documents(docs);
    dataset.graph_sage_status = GS_EMBEDDED;
    dataset.graph_sage_message = "Mean-aggregation GraphSAGE-style representations stored. Not used in the headline AI share.";
    store.save_dataset(dataset);
}

nlohmann::json train_graphsage(Store& store, int64_t dataset_id, const std::string& labels_path) {
    auto ds = store.get_dataset(dataset_id);
    if (!ds) throw std::runtime_error("dataset not found");
    if (labels_path.empty()) {
        ds->graph_sage_status = GS_NOT_TRAINED;
        ds->graph_sage_message =
            "Labeled human/AI examples are required before GraphSAGE can be treated as a detection signal. Headline estimates still use calibrated stylometry, detectors, embedding anomalies, and baseline deviation only.";
        store.save_dataset(*ds);
    } else {
        ds->graph_sage_status = GS_VALIDATION_FAILED;
        ds->graph_sage_message =
            "A labels file was provided, but v1 does not promote GraphSAGE into the headline until hold-out validation is implemented and passing. Representations may still be used for graph exploration.";
        store.save_dataset(*ds);
    }
    return json{
        {"status", ds->graph_sage_status},
        {"message", ds->graph_sage_message},
        {"usedInHeadline", false}
    };
}

nlohmann::json analyze_dataset(Store& store, const Config& cfg, int64_t dataset_id, bool incremental) {
    auto ds = store.get_dataset(dataset_id);
    if (!ds) throw std::runtime_error("dataset not found");
    auto docs = store.docs_by_dataset(dataset_id, false);
    if (docs.empty()) {
        ds->analysis_state = "empty";
        store.save_dataset(*ds);
        return json{{"id", ds->id}, {"analysisState", "empty"}};
    }
    bool pending = false;
    for (const auto& d : docs) {
        if (!d.p_ai || d.embedding.empty()) pending = true;
    }
    if (incremental && !pending) {
        ds->analysis_state = "ready";
        if (ds->last_analyzed_at.empty()) ds->last_analyzed_at = now_iso();
        store.save_dataset(*ds);
        return json{{"id", ds->id}, {"analysisState", "ready"}};
    }
    ds->analysis_state = "running";
    store.save_dataset(*ds);
    auto& rt = analysis_runtime();
    int64_t wf = rt.submit_analysis(dataset_id, !incremental);
    rt.wait(wf);
    auto prog = rt.progress_json(wf);
    ds = store.get_dataset(dataset_id);
    if (prog.value("status", "") == "failed") {
        if (ds) {
            ds->analysis_state = "failed";
            store.save_dataset(*ds);
        }
        throw std::runtime_error("analysis workflow failed");
    }
    return json{
        {"id", ds->id},
        {"analysisState", ds->analysis_state},
        {"workflowId", wf},
        {"workflow", prog}
    };
}

}  // namespace kos
