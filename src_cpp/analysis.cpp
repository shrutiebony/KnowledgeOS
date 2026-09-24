#include "analysis.hpp"
#include "gdelt_crawl.hpp"
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

namespace kos {

using nlohmann::json;

static double nz(const std::optional<double>& v) { return v ? *v : 0; }

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

static double se_of(const std::vector<double>& values) {
    if (values.size() < 2) return 0.08;
    double mean = 0;
    for (double v : values) mean += v;
    mean /= static_cast<double>(values.size());
    double var = 0;
    for (double v : values) var += (v - mean) * (v - mean);
    return std::sqrt(var / static_cast<double>(values.size() - 1)) / std::sqrt(static_cast<double>(values.size()));
}

struct Interval {
    double mean = 0;
    double low = 0;
    double high = 0;
};

static Interval mean_interval(const std::vector<double>& values) {
    if (values.empty()) return {};
    double mean = 0;
    for (double v : values) mean += v;
    mean /= static_cast<double>(values.size());
    double err = 1.96 * se_of(values);
    return {mean, clamp01(mean - err), clamp01(mean + err)};
}

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

static double parse_signal(const std::string& json_s, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto i = json_s.find(needle);
    if (i == std::string::npos) return 0;
    size_t start = i + needle.size();
    size_t end = start;
    while (end < json_s.size() && std::string("0123456789.+-eE").find(json_s[end]) != std::string::npos) end++;
    try {
        return std::stod(json_s.substr(start, end - start));
    } catch (...) {
        return 0;
    }
}

static json view_signals_json(const std::vector<Document>& docs) {
    double p = 0, sty = 0, ngram = 0, uni = 0, emb = 0, dev = 0, stock = 0, rec = 0;
    int np = 0, nsty = 0, nng = 0, nuni = 0, nemb = 0, ndev = 0, nstock = 0, nrec = 0;
    for (const auto& d : docs) {
        if (d.p_ai) { p += *d.p_ai; np++; }
        if (d.detector_stylometry) { sty += *d.detector_stylometry; nsty++; }
        if (d.detector_repetition) { ngram += *d.detector_repetition; nng++; }
        if (d.detector_uniformity) { uni += *d.detector_uniformity; nuni++; }
        if (d.embedding_anomaly) { emb += *d.embedding_anomaly; nemb++; }
        if (d.stylometry_deviation) { dev += *d.stylometry_deviation; ndev++; }
        if (d.explanation_json.find("stock_phrase_detector") != std::string::npos) {
            stock += parse_signal(d.explanation_json, "stock_phrase_detector");
            nstock++;
        }
        if (d.explanation_json.find("post_chatgpt") != std::string::npos) {
            rec += parse_signal(d.explanation_json, "post_chatgpt");
            nrec++;
        }
    }
    auto avg = [](double s, int n) { return n ? round3(s / n) : 0.0; };
    return {
        {"pAi", avg(p, np)},
        {"stylometry", avg(sty, nsty)},
        {"stockPhrases", avg(stock, nstock)},
        {"uniformity", avg(uni, nuni)},
        {"ngrams", avg(ngram, nng)},
        {"embeddingAnomaly", avg(emb, nemb)},
        {"stylometryDeviation", avg(dev, ndev)},
        {"postChatgpt", avg(rec, nrec)}
    };
}

static int year_of_date(const std::string& date) {
    auto is_year = [](int y) { return y >= 1900 && y <= 2100; };
    if (date.size() >= 4) {
        bool digits = true;
        for (int i = 0; i < 4; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(date[static_cast<size_t>(i)]))) {
                digits = false;
                break;
            }
        }
        if (digits) {
            try {
                int y = std::stoi(date.substr(0, 4));
                if (is_year(y)) return y;
            } catch (...) {
            }
        }
    }
    for (size_t i = 0; i + 3 < date.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(date[i])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 1])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 2])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 3]))) {
            continue;
        }
        const bool left_ok = i == 0 || !std::isdigit(static_cast<unsigned char>(date[i - 1]));
        const bool right_ok = i + 4 >= date.size() || !std::isdigit(static_cast<unsigned char>(date[i + 4]));
        if (!left_ok || !right_ok) continue;
        try {
            int y = std::stoi(date.substr(i, 4));
            if (is_year(y)) return y;
        } catch (...) {
        }
    }
    return 0;
}

// Best available document date: first-revision / createdAt, else publishedAt.
static std::string doc_date_of(const Document& d) {
    if (!d.created_at.empty() && year_of_date(d.created_at) > 0) return d.created_at;
    if (!d.published_at.empty() && year_of_date(d.published_at) > 0) return d.published_at;
    return "";
}

static std::string chart_date_of(const Document& d) { return doc_date_of(d); }

static int doc_year(const Document& d) { return year_of_date(doc_date_of(d)); }

static bool is_pre_2019(const Document& d) {
    int y = doc_year(d);
    return y > 0 && y < ERA_SPLIT_YEAR;
}

static bool is_post_2019(const Document& d) { return doc_year(d) >= ERA_SPLIT_YEAR; }

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
static void apply_era_scores(std::vector<Document>& docs, const Config& cfg, bool allow_keep_stored) {
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
    for (auto& doc : docs) {
        Features f = stylometry_analyze(doc.text);
        doc.word_count = f.word_count;
        doc.type_token_ratio = f.type_token_ratio;
        doc.avg_sentence_length = f.avg_sentence_length;
        doc.sentence_length_std = f.sentence_length_std;
        doc.burstiness = f.burstiness;
        doc.punctuation_ratio = f.punctuation_ratio;
        doc.char_entropy = f.char_entropy;
        doc.repetition_score = f.repetition_score;
        doc.embedding = hashed_embed(doc.title + "\n" + doc.text, cfg.embed_dim);
        doc.detector_stylometry = stylometry_ai_score(f);
        double stock = 0;
        for (const auto& det : run_detectors(doc.text, f)) {
            if (det.name == "stock_phrase_detector") stock = det.score;
            else if (det.name == "ngram_repetition_detector") doc.detector_repetition = det.score;
            else if (det.name == "sentence_uniformity_detector") doc.detector_uniformity = det.score;
        }
        doc.explanation_json = signals_json({{"stock_phrase_raw", stock}, {"stock_phrase_detector", stock}});
    }
    apply_era_scores(docs, cfg, false);
}

static std::string edge_reason(bool topic, bool source) {
    std::string r = "embedding";
    if (topic) r += "+topic";
    if (source) r += "+source";
    return r;
}

struct ScoredNeighbor {
    size_t j;
    double score;
    double cos;
    bool topic;
    bool source;
};

static std::vector<ScoredNeighbor> knn_neighbors(const Document& a, const std::vector<Document>& docs,
                                                int k, double min_cos) {
    std::vector<ScoredNeighbor> neighbors;
    for (size_t j = 0; j < docs.size(); ++j) {
        const auto& b = docs[j];
        if (a.id == b.id) continue;
        double cos = cosine(a.embedding, b.embedding);
        bool topic_match = !a.topic.empty() && a.topic == b.topic;
        bool source_match = !a.source.empty() && a.source == b.source;
        double score = cos + (topic_match ? 0.08 : 0) + (source_match ? 0.04 : 0);
        if (cos >= min_cos || topic_match) {
            neighbors.push_back({j, score, cos, topic_match, source_match});
        }
    }
    std::sort(neighbors.begin(), neighbors.end(),
              [](const ScoredNeighbor& x, const ScoredNeighbor& y) { return x.score > y.score; });
    if (k >= 0 && static_cast<int>(neighbors.size()) > k) neighbors.resize(k);
    return neighbors;
}

static std::vector<DocumentEdge> knn_edges(const std::vector<Document>& docs, int k, double min_cos,
                                          int64_t edge_dataset_id) {
    std::vector<DocumentEdge> created;
    for (const auto& a : docs) {
        for (const auto& s : knn_neighbors(a, docs, k, min_cos)) {
            DocumentEdge e;
            e.dataset_id = edge_dataset_id;
            e.source_document_id = a.id;
            e.target_document_id = docs[s.j].id;
            e.cosine = s.cos;
            e.reason = edge_reason(s.topic, s.source);
            created.push_back(e);
        }
    }
    return created;
}

static void rebuild_graph(Store& store, int64_t dataset_id, const std::vector<Document>& docs, const Config& cfg) {
    store.delete_edges(dataset_id);
    store.insert_edges(knn_edges(docs, cfg.graph_k, cfg.graph_min_cosine, dataset_id));
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
    auto docs = store.docs_by_dataset(dataset_id, true);
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
    assign_scores(docs, cfg);
    store.save_documents(docs);
    rebuild_graph(store, dataset_id, docs, cfg);
    if (!incremental) mean_aggregate_graphsage(store, *ds);
    ds = store.get_dataset(dataset_id);
    ds->last_analyzed_at = now_iso();
    ds->analysis_state = "ready";
    store.save_dataset(*ds);
    return json{{"id", ds->id}, {"analysisState", "ready"}};
}

static bool is_union_scope(int64_t dataset_id) {
    return dataset_id == DATASET_ALL;
}

static bool collection_is_wikipedia(Store& store, int64_t dataset_id) {
    if (is_union_scope(dataset_id)) return false;
    auto ds = store.get_dataset(dataset_id);
    if (!ds) return false;
    return ds->kind == KIND_WIKI || ds->kind == KIND_WIKI_SUBSET || is_wikipedia_name(ds->name);
}

static std::unordered_set<int64_t> wikipedia_dataset_ids(Store& store) {
    std::unordered_set<int64_t> out;
    for (const auto& d : store.all_datasets()) {
        if (d.kind == KIND_WIKI || d.kind == KIND_WIKI_SUBSET || is_wikipedia_name(d.name)) {
            out.insert(d.id);
        }
    }
    return out;
}

static bool document_is_wikipedia(const Document& d, const std::unordered_set<int64_t>& wiki_ids) {
    if (wiki_ids.count(d.dataset_id)) return true;
    return iequals(d.source, WIKI_SOURCE);
}

// Display-only: stored "Geography of India" is shown and grouped as India.
static std::string display_wiki_topic(const std::string& topic, const std::string& title = "") {
    std::string t = canonical_wiki_topic(topic, title);
    if (iequals(t, SHARED_TOPIC_GEO_INDIA)) return SHARED_TOPIC_INDIA;
    return t;
}

static std::string collapse_geo_india_label(const std::string& topic) {
    return iequals(trim(topic), SHARED_TOPIC_GEO_INDIA) ? SHARED_TOPIC_INDIA : topic;
}

static bool is_display_country_topic(const std::string& topic) {
    std::string t = collapse_geo_india_label(trim(topic));
    return iequals(t, SHARED_TOPIC_INDIA) || iequals(t, SHARED_TOPIC_USA) ||
           iequals(t, SHARED_TOPIC_GERMANY) || iequals(t, SHARED_TOPIC_AUSTRALIA);
}

static std::string pretty_subject_topic(const std::string& topic) {
    std::string t = trim(topic);
    if (t.empty() || is_display_country_topic(t)) return "";
    if (iequals(t, "computer_science")) return "Computer science";
    if (iequals(t, "general")) return "";
    std::string out;
    bool cap = true;
    for (char ch : t) {
        if (ch == '_') {
            out.push_back(' ');
            cap = true;
            continue;
        }
        unsigned char uc = static_cast<unsigned char>(ch);
        if (cap && std::isalpha(uc)) {
            out.push_back(static_cast<char>(std::toupper(uc)));
            cap = false;
        } else {
            out.push_back(ch);
            if (ch == ' ') cap = true;
        }
    }
    return out;
}

static void apply_country_topic(Document& d) {
    std::string original = trim(d.topic);
    std::string shown = collapse_geo_india_label(display_wiki_topic(original, d.title));
    if (is_display_country_topic(shown)) {
        if (!is_display_country_topic(original)) d.subtopic = pretty_subject_topic(original);
        d.topic = shown;
        return;
    }
    std::string inferred = classify_country_topic(d.title, original);
    if (inferred.empty()) inferred = classify_country_topic(d.title, d.title);
    if (!inferred.empty() && is_display_country_topic(inferred)) {
        d.subtopic = pretty_subject_topic(original);
        d.topic = collapse_geo_india_label(inferred);
        return;
    }
    d.subtopic = pretty_subject_topic(original.empty() ? shown : original);
    d.topic.clear();
}

static void apply_country_topics(std::vector<Document>& docs) {
    for (auto& d : docs) apply_country_topic(d);
}

static void remap_wiki_topics(std::vector<Document>& docs) {
    for (auto& d : docs) d.topic = display_wiki_topic(d.topic, d.title);
}

static void remap_wiki_topics_in_union(std::vector<Document>& docs,
                                       const std::unordered_set<int64_t>& wiki_ids) {
    for (auto& d : docs) {
        if (document_is_wikipedia(d, wiki_ids)) d.topic = display_wiki_topic(d.topic, d.title);
    }
}

static std::string topic_needle(const std::string& wanted) {
    std::string t = trim(wanted);
    if (t.empty()) return "";
    return ascii_lower(display_wiki_topic(t, t));
}

static bool document_matches_topic(const Document& d, const std::string& needle) {
    if (needle.empty()) return true;
    std::string stored = ascii_lower(collapse_geo_india_label(trim(d.topic)));
    if (!stored.empty() && stored == needle) return true;
    std::string canon = ascii_lower(display_wiki_topic(d.topic, d.title));
    return canon == needle;
}

std::vector<Document> docs_for_view(Store& store, int64_t dataset_id, const std::string& topic,
                                    const std::vector<int64_t>& ids) {
    std::vector<Document> docs;
    std::unordered_set<int64_t> wiki_ids;
    const bool union_view = is_union_scope(dataset_id);
    if (union_view) {
        docs = store.all_docs(false);
        wiki_ids = wikipedia_dataset_ids(store);
        remap_wiki_topics_in_union(docs, wiki_ids);
    } else {
        docs = store.docs_by_dataset(dataset_id, false);
        if (collection_is_wikipedia(store, dataset_id)) remap_wiki_topics(docs);
    }
    apply_country_topics(docs);
    std::string wanted = trim(topic);
    if (!wanted.empty()) {
        std::string needle = topic_needle(wanted);
        if (needle.empty()) needle = ascii_lower(wanted);
        docs.erase(std::remove_if(docs.begin(), docs.end(), [&](const Document& d) {
            return !document_matches_topic(d, needle);
        }), docs.end());
    }
    if (!ids.empty()) {
        std::unordered_set<int64_t> keep(ids.begin(), ids.end());
        docs.erase(std::remove_if(docs.begin(), docs.end(), [&](const Document& d) {
            return !keep.count(d.id);
        }), docs.end());
    }
    return docs;
}

std::vector<std::string> topics_for_dataset(Store& store, int64_t dataset_id) {
    std::vector<Document> docs;
    bool canonicalize = false;
    if (is_union_scope(dataset_id)) {
        docs = store.all_docs(false);
        auto wiki_ids = wikipedia_dataset_ids(store);
        remap_wiki_topics_in_union(docs, wiki_ids);
        canonicalize = true;
    } else {
        docs = store.docs_by_dataset(dataset_id, false);
        canonicalize = collection_is_wikipedia(store, dataset_id);
        if (canonicalize) remap_wiki_topics(docs);
    }
    apply_country_topics(docs);
    std::map<std::string, int> counts;
    for (const auto& d : docs) {
        std::string key = collapse_geo_india_label(trim(d.topic));
        if (is_blank(key) || !is_display_country_topic(key)) continue;
        counts[key]++;
    }
    std::vector<std::string> out;
    for (const auto& [name, n] : counts) {
        if (n >= MIN_TOPIC_DOCUMENTS) out.push_back(name);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return ascii_lower(a) < ascii_lower(b);
    });
    return out;
}

static std::string band_heading(const std::string& band) {
    if (band == BAND_AI) return "likely AI";
    if (band == BAND_HUMAN) return "likely human";
    if (band == BAND_UNC) return "uncertain";
    return "pending";
}

static std::string collection_label_of(const Dataset& d) {
    if (d.kind == KIND_WIKI || d.kind == KIND_WIKI_SUBSET || is_wikipedia_name(d.name)) return "Wikipedia";
    if (d.kind == KIND_GDELT || iequals(trim(d.name), "GDELT")) return "GDELT";
    return d.name.empty() ? "Collection" : d.name;
}

static std::unordered_map<int64_t, std::string> collection_labels(Store& store) {
    std::unordered_map<int64_t, std::string> out;
    for (const auto& d : store.all_datasets()) out[d.id] = collection_label_of(d);
    return out;
}

static std::string choose_group_by(const std::vector<Document>& docs, const std::string& topic_filter) {
    if (!trim(topic_filter).empty()) return "dataset";
    std::map<std::string, int> topic_counts;
    for (const auto& d : docs) {
        if (!is_blank(d.topic) && is_display_country_topic(d.topic)) {
            topic_counts[ascii_lower(d.topic)]++;
        }
    }
    int substantial_topics = 0;
    for (const auto& [topic, n] : topic_counts) {
        if (n >= MIN_TOPIC_DOCUMENTS) substantial_topics++;
    }
    if (substantial_topics >= 1) return "topic";
    return "dataset";
}

static std::string graph_group_label(const Document& doc, const std::string& group_by,
                                    const std::unordered_map<int64_t, std::string>& collections) {
    if (group_by == "dataset") {
        auto it = collections.find(doc.dataset_id);
        if (it != collections.end() && !it->second.empty()) return it->second;
        return "Unknown collection";
    }
    if (is_blank(doc.topic) || !is_display_country_topic(doc.topic)) return "Uncategorized";
    return doc.topic;
}

static json card_json(Store& store, const Document& meta, bool include_text) {
    Document doc = meta;
    if (include_text || doc.text.empty()) {
        auto full = store.get_document(meta.id, true);
        if (full) doc = *full;
    }
    json m;
    m["id"] = doc.id;
    m["title"] = doc.title;
    m["url"] = doc.url.empty() ? nullptr : json(doc.url);
    m["source"] = doc.source;
    apply_country_topic(doc);
    m["topic"] = collapse_geo_india_label(doc.topic);
    if (doc.subtopic.empty()) m["subtopic"] = nullptr;
    else m["subtopic"] = doc.subtopic;
    if (doc.published_at.empty()) m["publishedAt"] = nullptr;
    else m["publishedAt"] = doc.published_at;
    if (doc.created_at.empty()) m["createdAt"] = nullptr;
    else m["createdAt"] = doc.created_at;
    m["wordCount"] = doc.word_count;
    if (doc.p_ai) m["pAi"] = round3(*doc.p_ai);
    else m["pAi"] = nullptr;
    if (doc.ci_low) m["ciLow"] = round3(*doc.ci_low);
    else m["ciLow"] = nullptr;
    if (doc.ci_high) m["ciHigh"] = round3(*doc.ci_high);
    else m["ciHigh"] = nullptr;
    m["band"] = doc.band;
    m["signals"] = {
        {"stylometry", nz(doc.detector_stylometry)},
        {"stock_phrase_detector", parse_signal(doc.explanation_json, "stock_phrase_detector")},
        {"sentence_uniformity_detector", nz(doc.detector_uniformity)},
        {"ngram_repetition_detector", nz(doc.detector_repetition)},
        {"embedding_anomaly", nz(doc.embedding_anomaly)},
        {"stylometry_deviation", nz(doc.stylometry_deviation)},
        {"post_chatgpt", parse_signal(doc.explanation_json, "post_chatgpt")}
    };
    if (include_text) {
        m["text"] = doc.text;
    } else {
        m["excerpt"] = doc.text.substr(0, std::min<size_t>(320, doc.text.size()));
    }
    return m;
}

static json independent_headline(Store& store, const Dataset& other, const std::string& metric) {
    auto docs = store.docs_by_dataset(other.id, false);
    json m;
    m["id"] = other.id;
    m["name"] = other.name;
    m["kind"] = other.kind;
    m["documentCount"] = docs.size();
    std::vector<Document> scored;
    for (const auto& d : docs) if (d.p_ai) scored.push_back(d);
    if (scored.empty()) {
        m["headline"] = "Not yet analyzed";
        return m;
    }
    bool use_words = iequals(metric, "words");
    if (use_words) {
        long words = 0;
        double acc = 0;
        for (const auto& d : scored) {
            words += d.word_count;
            acc += *d.p_ai * d.word_count;
        }
        double word_mean = words == 0 ? 0 : acc / words;
        m["estimatePercent"] = pct(word_mean);
        m["headline"] = "Estimated AI-generated share of analyzed words: " + std::to_string(pct(word_mean)) + "%";
    } else {
        std::vector<double> ps;
        for (const auto& d : scored) ps.push_back(*d.p_ai);
        auto iv = mean_interval(ps);
        m["estimatePercent"] = pct(iv.mean);
        m["headline"] = "Estimated AI-generated share of documents: " + std::to_string(pct(iv.mean)) + "%";
    }
    return m;
}

nlohmann::json summary_json(Store& store, int64_t dataset_id, const std::string& metric,
                            const std::string& topic, const std::vector<int64_t>& ids) {
    const bool union_view = is_union_scope(dataset_id);
    std::optional<Dataset> ds;
    if (!union_view) {
        ds = store.get_dataset(dataset_id);
        if (!ds) throw std::runtime_error("dataset not found");
    }
    long corpus = union_view ? store.count_all_docs() : store.count_docs(dataset_id);
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    apply_era_scores(docs, Config{}, true);
    std::vector<Document> scored;
    for (const auto& d : docs) if (d.p_ai) scored.push_back(d);
    json out;
    if (union_view) {
        out["datasetId"] = "all";
        out["name"] = "All sources";
        out["kind"] = KIND_ALL;
        out["union"] = true;
        out["analysisState"] = scored.empty() ? "empty" : "ready";
        out["lastAnalyzedAt"] = nullptr;
        out["graphsage"] = {
            {"status", GS_NOT_TRAINED},
            {"message", GS_DEFAULT_MSG},
            {"usedInHeadline", false}
        };
    } else {
        out["datasetId"] = ds->id;
        out["name"] = ds->name;
        out["kind"] = ds->kind;
        out["union"] = false;
        out["analysisState"] = ds->analysis_state;
        if (ds->last_analyzed_at.empty()) out["lastAnalyzedAt"] = nullptr;
        else out["lastAnalyzedAt"] = ds->last_analyzed_at;
        out["graphsage"] = {
            {"status", ds->graph_sage_status},
            {"message", ds->graph_sage_message},
            {"usedInHeadline", false}
        };
    }
    std::string view_topic = trim(topic);
    if (view_topic.empty()) out["topic"] = nullptr;
    else out["topic"] = collapse_geo_india_label(view_topic);
    out["topicView"] = !view_topic.empty();
    out["idView"] = !ids.empty();
    out["corpusDocumentCount"] = corpus;
    out["documentCount"] = docs.size();
    out["disclaimer"] =
        "Estimates are not proof of authorship. Documents are labeled likely AI-generated, likely human-written, or uncertain.";
    json bands = {{BAND_AI, 0}, {BAND_HUMAN, 0}, {BAND_UNC, 0}};
    for (const auto& d : docs) {
        if (d.band == BAND_PENDING) continue;
        if (bands.contains(d.band)) bands[d.band] = bands[d.band].get<int>() + 1;
    }
    out["bands"] = bands;
    if (scored.empty()) {
        out["headlineMetric"] = metric.empty() ? "documents" : metric;
        out["headline"] = "Not yet analyzed";
        out["collectionSignals"] = view_signals_json(docs);
        return out;
    }
    out["collectionSignals"] = view_signals_json(scored);
    std::vector<double> p_docs;
    long words = 0;
    double word_acc = 0;
    for (const auto& d : scored) {
        p_docs.push_back(*d.p_ai);
        words += d.word_count;
        word_acc += *d.p_ai * d.word_count;
    }
    auto doc_share = mean_interval(p_docs);
    double word_mean = words == 0 ? 0 : word_acc / words;
    double word_se = se_of(p_docs);
    Interval word_share{word_mean, clamp01(word_mean - 1.96 * word_se), clamp01(word_mean + 1.96 * word_se)};
    bool use_words = iequals(metric, "words");
    out["analyzedWordCount"] = words;
    out["shareOfDocuments"] = pct(doc_share.mean);
    out["shareOfDocumentsRange"] = json::array({pct(doc_share.low), pct(doc_share.high)});
    out["shareOfAnalyzedWords"] = pct(word_mean);
    out["shareOfAnalyzedWordsRange"] = json::array({pct(word_share.low), pct(word_share.high)});
    out["headlineMetric"] = use_words ? "words" : "documents";
    if (use_words) {
        out["estimatePercent"] = pct(word_mean);
        out["rangePercent"] = json::array({pct(word_share.low), pct(word_share.high)});
        out["headline"] = "Estimated AI-generated share of analyzed words: " + std::to_string(pct(word_mean)) + "%";
    } else {
        out["estimatePercent"] = pct(doc_share.mean);
        out["rangePercent"] = json::array({pct(doc_share.low), pct(doc_share.high)});
        out["headline"] = "Estimated AI-generated share of documents: " + std::to_string(pct(doc_share.mean)) + "%";
    }
    if (!union_view && ds && ds->kind == KIND_URLS) {
        auto wiki = store.find_first_by_kind(KIND_WIKI);
        if (wiki) {
            json this_c;
            this_c["id"] = ds->id;
            this_c["name"] = ds->name;
            this_c["kind"] = ds->kind;
            this_c["documentCount"] = docs.size();
            this_c["estimatePercent"] = out["estimatePercent"];
            this_c["rangePercent"] = out["rangePercent"];
            this_c["headline"] = out["headline"];
            json comparison;
            comparison["label"] = "Same analysis methods; two independent collections. Graphs and baselines are not shared.";
            comparison["thisCollection"] = this_c;
            comparison["wikipediaSample"] = independent_headline(store, *wiki, metric);
            out["comparison"] = comparison;
        }
    }
    return out;
}

nlohmann::json graph_json(Store& store, int64_t dataset_id, const std::string& topic,
                          const std::vector<int64_t>& ids, int graph_k, double graph_min_cosine) {
    const bool union_view = is_union_scope(dataset_id);
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    apply_era_scores(docs, Config{}, true);
    auto collections = collection_labels(store);
    std::string group_by = choose_group_by(docs, topic);
    std::unordered_map<int64_t, std::string> groups_by_id;
    std::map<std::string, int> counts;
    for (const auto& doc : docs) {
        std::string label = graph_group_label(doc, group_by, collections);
        groups_by_id[doc.id] = label;
        counts[label]++;
    }
    if (group_by != "topic" && counts.size() > 8) {
        std::vector<std::pair<std::string, int>> rows(counts.begin(), counts.end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return ascii_lower(a.first) < ascii_lower(b.first);
        });
        std::unordered_set<std::string> keep;
        for (size_t i = 0; i < std::min<size_t>(7, rows.size()); ++i) keep.insert(rows[i].first);
        for (auto& [id, label] : groups_by_id) {
            if (!keep.count(label)) label = "Other (remaining groups)";
        }
        counts.clear();
        for (const auto& [id, label] : groups_by_id) counts[label]++;
    }
    json nodes = json::array();
    std::unordered_set<int64_t> keep;
    for (const auto& doc : docs) {
        keep.insert(doc.id);
        json n;
        n["id"] = doc.id;
        n["title"] = doc.title;
        n["band"] = doc.band;
        if (doc.p_ai) n["pAi"] = *doc.p_ai;
        else n["pAi"] = nullptr;
        n["wordCount"] = doc.word_count;
        n["topic"] = collapse_geo_india_label(doc.topic);
        if (doc.subtopic.empty()) n["subtopic"] = nullptr;
        else n["subtopic"] = doc.subtopic;
        n["source"] = doc.source;
        n["datasetId"] = doc.dataset_id;
        auto cit = collections.find(doc.dataset_id);
        n["dataset"] = cit != collections.end() ? cit->second : "Unknown collection";
        if (doc.created_at.empty()) n["createdAt"] = nullptr;
        else n["createdAt"] = doc.created_at;
        if (doc.published_at.empty()) n["publishedAt"] = nullptr;
        else n["publishedAt"] = doc.published_at;
        n["group"] = groups_by_id[doc.id];
        nodes.push_back(n);
    }
    std::vector<std::pair<std::string, int>> group_rows(counts.begin(), counts.end());
    std::sort(group_rows.begin(), group_rows.end(), [](auto& a, auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return ascii_lower(a.first) < ascii_lower(b.first);
    });
    json groups = json::array();
    for (const auto& [k, c] : group_rows) {
        if ((group_by == "topic" || group_by == "subtopic") &&
            (c < MIN_TOPIC_DOCUMENTS || k == "Other" ||
             k.rfind("Other (", 0) == 0 || k == "Uncategorized")) continue;
        groups.push_back({{"key", k}, {"label", k}, {"count", c}});
    }
    json links = json::array();
    std::vector<DocumentEdge> edges = union_view
        ? knn_edges(docs, graph_k, graph_min_cosine, DATASET_ALL)
        : store.edges_by_dataset(dataset_id);
    for (const auto& e : edges) {
        if (!keep.count(e.source_document_id) || !keep.count(e.target_document_id)) continue;
        links.push_back({{"from", e.source_document_id}, {"to", e.target_document_id},
                         {"cosine", round3(e.cosine)}, {"reason", e.reason}});
    }
    json out;
    if (union_view) out["datasetId"] = "all";
    else out["datasetId"] = dataset_id;
    out["union"] = union_view;
    if (trim(topic).empty()) out["topic"] = nullptr;
    else out["topic"] = collapse_geo_india_label(trim(topic));
    out["idView"] = !ids.empty();
    out["groupBy"] = group_by;
    out["groups"] = groups;
    out["nodes"] = nodes;
    out["edges"] = links;
    return out;
}

static std::string topic_band_from_mean(double mean) {
    if (mean >= TOPIC_BAND_AI_MIN) return BAND_AI;
    if (mean <= TOPIC_BAND_HUMAN_MAX) return BAND_HUMAN;
    return BAND_UNC;
}

nlohmann::json examples_json(Store& store, int64_t dataset_id, const std::string& band, int limit,
                             const std::string& topic, const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    apply_era_scores(docs, Config{}, true);
    std::map<std::string, std::vector<Document>> groups;
    for (const auto& d : docs) {
        std::string key = trim(d.topic);
        if (key.empty() || !is_display_country_topic(key) || !d.p_ai) continue;
        groups[key].push_back(d);
    }
    struct TopicRow {
        std::string topic;
        double mean = 0;
        int n = 0;
        std::string band;
    };
    std::vector<TopicRow> rows;
    for (auto& [k, g] : groups) {
        if (static_cast<int>(g.size()) < MIN_TOPIC_DOCUMENTS) continue;
        std::vector<double> ps;
        for (const auto& d : g) ps.push_back(*d.p_ai);
        auto iv = mean_interval(ps);
        TopicRow row{k, iv.mean, static_cast<int>(g.size()), topic_band_from_mean(iv.mean)};
        if (!band.empty() && row.band != band) continue;
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [&](const TopicRow& a, const TopicRow& b) {
        if (band == BAND_AI) return a.mean > b.mean;
        if (band == BAND_HUMAN) return a.mean < b.mean;
        if (a.n != b.n) return a.n > b.n;
        return ascii_lower(a.topic) < ascii_lower(b.topic);
    });
    if (limit > 0 && static_cast<int>(rows.size()) > limit) rows.resize(limit);
    json out = json::array();
    for (const auto& row : rows) {
        out.push_back({{"topic", row.topic},
                       {"title", row.topic},
                       {"pAi", round3(row.mean)},
                       {"documentCount", row.n},
                       {"band", row.band}});
    }
    return out;
}

static std::vector<std::string> tokens_alnum(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : ascii_lower(text)) {
        if (std::isalnum(c) || c == '\'') cur.push_back(static_cast<char>(c));
        else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool is_filler_word(const std::string& w) {
    static const std::unordered_set<std::string> stop = {
        "the", "a", "an", "and", "or", "of", "to", "in", "on", "for", "with", "by", "from", "at",
        "as", "is", "are", "was", "were", "be", "been", "being", "this", "that", "these", "those",
        "it", "its", "their", "they", "them", "we", "you", "your", "our", "not", "but", "if",
        "than", "then", "also", "into", "over", "after", "before", "about", "more", "most", "can",
        "will", "has", "have", "had", "his", "her", "she", "he", "which", "who", "whom", "what",
        "when", "where", "how", "all", "any", "each", "other", "such", "only", "own", "same", "so",
        "too", "very", "just", "because", "while", "through", "during", "without", "within",
        "between", "under", "again", "further", "once", "here", "there", "both", "few", "some",
        "no", "nor", "do", "does", "did", "doing", "would", "should", "could", "may", "might",
        "page", "pages", "see", "http", "https", "www", "com", "org", "html", "pdf", "new", "one",
        "two", "first", "last", "used", "using", "use", "including", "include", "based"
    };
    return w.size() < 3 || stop.count(w);
}

static json freq_rows(const std::map<std::string, int>& df, int limit, int min_df, const char* key) {
    std::vector<std::pair<std::string, int>> rows(df.begin(), df.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    json out = json::array();
    for (const auto& row : rows) {
        if (row.second < min_df) continue;
        out.push_back({{key, row.first}, {"documents", row.second}});
        if (static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

static json strongest_signal_row(const json& sig) {
    struct Row {
        const char* key;
        const char* label;
    };
    static const Row rows[] = {
        {"embeddingAnomaly", "unusual wording (embedding)"},
        {"stockPhrases", "stock phrases"},
        {"ngrams", "repeated phrases"},
        {"stylometry", "writing style"},
        {"uniformity", "even sentences"},
        {"stylometryDeviation", "writing style that differs from typical pages here"}
    };
    const char* best_key = nullptr;
    const char* best_label = nullptr;
    double best = -1;
    for (const auto& row : rows) {
        if (!sig.contains(row.key) || !sig[row.key].is_number()) continue;
        double v = sig[row.key].get<double>();
        if (v > best) {
            best = v;
            best_key = row.key;
            best_label = row.label;
        }
    }
    if (!best_key) return nullptr;
    return {{"key", best_key}, {"label", best_label}, {"score", round3(best)}};
}

nlohmann::json ai_common_json(Store& store, int64_t dataset_id, const std::string& topic,
                              const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    apply_era_scores(docs, Config{}, true);
    std::vector<Document> ai;
    for (const auto& d : docs) {
        if (d.band == BAND_AI) ai.push_back(d);
    }
    json out;
    out["documentCount"] = static_cast<int>(ai.size());
    if (ai.empty()) {
        out["signals"] = nullptr;
        out["strongest"] = nullptr;
        out["stockPhrases"] = json::array();
        out["words"] = json::array();
        out["phrases"] = json::array();
        return out;
    }
    json signals = view_signals_json(ai);
    out["signals"] = signals;
    out["strongest"] = strongest_signal_row(signals);

    std::sort(ai.begin(), ai.end(), [](const Document& a, const Document& b) {
        return nz(a.p_ai) > nz(b.p_ai);
    });
    const size_t take = std::min<size_t>(220, ai.size());
    std::vector<int64_t> sample;
    sample.reserve(take);
    for (size_t i = 0; i < take; ++i) sample.push_back(ai[i].id);
    auto texts = store.docs_by_ids(sample, true);

    static const char* markers[] = {
        "it is important to note",
        "in conclusion",
        "this article provides",
        "plays a crucial role",
        "in today's world",
        "a comprehensive overview",
        "it should be noted",
        "various factors",
        "in this article we will",
        "delve into"
    };
    std::map<std::string, int> stock_df, word_df, phrase_df;
    for (const auto& d : texts) {
        std::string blob = d.title;
        blob += "\n";
        blob += d.text.size() > 6000 ? d.text.substr(0, 6000) : d.text;
        std::string lower = ascii_lower(blob);
        std::unordered_set<std::string> seen_stock, seen_word, seen_phrase;
        for (const char* m : markers) {
            if (lower.find(m) != std::string::npos) seen_stock.insert(m);
        }
        auto toks = tokens_alnum(blob);
        for (const auto& w : toks) {
            if (!is_filler_word(w)) seen_word.insert(w);
        }
        for (size_t i = 0; i + 1 < toks.size(); ++i) {
            if (toks[i].size() < 3 || toks[i + 1].size() < 3) continue;
            if (is_filler_word(toks[i]) && is_filler_word(toks[i + 1])) continue;
            seen_phrase.insert(toks[i] + " " + toks[i + 1]);
        }
        for (size_t i = 0; i + 2 < toks.size(); ++i) {
            int content = (!is_filler_word(toks[i]) ? 1 : 0) +
                          (!is_filler_word(toks[i + 1]) ? 1 : 0) +
                          (!is_filler_word(toks[i + 2]) ? 1 : 0);
            if (content < 2) continue;
            seen_phrase.insert(toks[i] + " " + toks[i + 1] + " " + toks[i + 2]);
        }
        for (const auto& s : seen_stock) stock_df[s]++;
        for (const auto& s : seen_word) word_df[s]++;
        for (const auto& s : seen_phrase) phrase_df[s]++;
    }
    int scanned = static_cast<int>(texts.size());
    int min_word = std::max(3, (scanned * 8) / 100);
    int min_phrase = std::max(3, (scanned * 6) / 100);
    out["sampled"] = scanned;
    out["stockPhrases"] = freq_rows(stock_df, 4, 2, "phrase");
    out["words"] = freq_rows(word_df, 6, min_word, "word");
    out["phrases"] = freq_rows(phrase_df, 5, min_phrase, "phrase");
    return out;
}

nlohmann::json breakdown_rows(Store& store, int64_t dataset_id, const std::string& by,
                              const std::string& topic, const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    apply_era_scores(docs, Config{}, true);
    json rows = json::array();
    if (by == "time") {
        std::map<int, std::vector<double>> scored;
        std::map<int, int> dated_count;
        int min_y = 0;
        int max_y = 0;
        for (const auto& doc : docs) {
            int y = year_of_date(chart_date_of(doc));
            if (y < TIME_CHART_MIN_YEAR) continue;
            if (!min_y || y < min_y) min_y = y;
            if (y > max_y) max_y = y;
            dated_count[y]++;
            if (doc.p_ai) scored[y].push_back(*doc.p_ai);
        }
        if (!min_y) return rows;
        min_y = TIME_CHART_MIN_YEAR;
        {
            const int now_y = utc_year_now();
            if (now_y > 0 && max_y > now_y) max_y = now_y;
        }
        for (int y = min_y; y <= max_y; ++y) {
            int n = dated_count[y];
            json row;
            row["key"] = std::to_string(y);
            row["documentCount"] = n;
            auto it = scored.find(y);
            if (it != scored.end() && !it->second.empty()) {
                auto iv = mean_interval(it->second);
                row["estimatePercent"] = pct(iv.mean);
                row["rangePercent"] = json::array({pct(iv.low), pct(iv.high)});
            } else {
                row["estimatePercent"] = nullptr;
                row["rangePercent"] = json::array();
            }
            rows.push_back(row);
        }
        return rows;
    }
    std::map<std::string, std::vector<Document>> groups;
    for (const auto& doc : docs) {
        if (!doc.p_ai) continue;
        std::string key = by == "topic" ? collapse_geo_india_label(trim(doc.topic)) : trim(doc.source);
        if (key.empty()) continue;
        if (by == "topic" && !is_display_country_topic(key)) continue;
        groups[key].push_back(doc);
    }
    for (auto& [k, g] : groups) {
        if (by == "topic" && static_cast<int>(g.size()) < MIN_TOPIC_DOCUMENTS) continue;
        std::vector<double> ps;
        for (const auto& d : g) ps.push_back(*d.p_ai);
        auto iv = mean_interval(ps);
        rows.push_back({{"key", k}, {"documentCount", g.size()}, {"estimatePercent", pct(iv.mean)},
                        {"rangePercent", json::array({pct(iv.low), pct(iv.high)})}});
    }
    return rows;
}

nlohmann::json explanation_json(Store& store, int64_t dataset_id, int64_t doc_id, int graph_k,
                                double graph_min_cosine) {
    auto doc = store.get_document(doc_id, true);
    if (!doc) throw std::runtime_error("document not found");
    const bool union_view = is_union_scope(dataset_id);
    if (!union_view && doc->dataset_id != dataset_id) {
        throw std::invalid_argument("Document is not in this dataset");
    }
    json out = card_json(store, *doc, true);
    json neighbors = json::array();
    if (union_view) {
        auto docs = docs_for_view(store, DATASET_ALL, "", {});
        for (const auto& s : knn_neighbors(*doc, docs, graph_k, graph_min_cosine)) {
            json row = card_json(store, docs[s.j], false);
            row["cosine"] = round3(s.cos);
            row["reason"] = edge_reason(s.topic, s.source);
            neighbors.push_back(row);
        }
    } else {
        for (const auto& e : store.edges_from(dataset_id, doc_id)) {
            auto n = store.get_document(e.target_document_id, false);
            if (!n) continue;
            json row = card_json(store, *n, false);
            row["cosine"] = round3(e.cosine);
            row["reason"] = e.reason;
            neighbors.push_back(row);
        }
    }
    out["neighbors"] = neighbors;
    out["disclaimer"] = "This is an estimate from calibrated signals, not proof of authorship.";
    return out;
}

static std::string era_date_of(const Document& d) {
    return doc_date_of(d);
}

static const char* era_date_source(const Document& d) {
    if (!d.created_at.empty() && year_of_date(d.created_at) > 0) return "createdAt";
    if (!d.published_at.empty() && year_of_date(d.published_at) > 0) return "publishedAt";
    return "";
}

static bool has_created_date(const Document& d) {
    return doc_year(d) > 0;
}

static std::vector<Document> sample_evenly(std::vector<Document> docs, int n) {
    std::sort(docs.begin(), docs.end(), [](const Document& a, const Document& b) {
        std::string da = era_date_of(a);
        std::string db = era_date_of(b);
        if (da != db) return da < db;
        return a.id < b.id;
    });
    if (static_cast<int>(docs.size()) <= n) return docs;
    std::vector<Document> out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        size_t idx = static_cast<size_t>((i * (static_cast<int>(docs.size()) - 1)) / std::max(1, n - 1));
        out.push_back(docs[idx]);
    }
    return out;
}

static double mean_opt(const std::vector<Document>& docs, const std::optional<double> Document::* field) {
    double s = 0;
    int n = 0;
    for (const auto& d : docs) {
        const auto& v = d.*field;
        if (!v) continue;
        s += *v;
        n++;
    }
    return n ? s / n : 0;
}

static double mean_stock(const std::vector<Document>& docs) {
    double s = 0;
    int n = 0;
    for (const auto& d : docs) {
        if (d.explanation_json.find("stock_phrase_detector") == std::string::npos) continue;
        s += parse_signal(d.explanation_json, "stock_phrase_detector");
        n++;
    }
    return n ? s / n : 0;
}

static double mean_pre_centroid_dist(const std::vector<Document>& docs, const std::vector<float>& pre_cent) {
    if (pre_cent.empty()) return 0;
    double s = 0;
    int n = 0;
    for (const auto& d : docs) {
        if (d.embedding.empty()) continue;
        s += 1.0 - cosine(d.embedding, pre_cent);
        n++;
    }
    return n ? s / n : 0;
}

struct EraTrait {
    std::string key;
    std::string label;
    double before = 0;
    double after = 0;
    bool higher_is_ai = true;
};

static bool trait_ai_like(const EraTrait& t) {
    const double eps = 0.02;
    if (t.higher_is_ai) return t.after > t.before + eps;
    return t.after + eps < t.before;
}

static json era_article_json(const Document& d) {
    json row;
    row["id"] = d.id;
    row["title"] = d.title;
    std::string date = era_date_of(d);
    int year = year_of_date(date);
    if (year > 0) row["year"] = year;
    else row["year"] = nullptr;
    if (date.empty()) row["date"] = nullptr;
    else row["date"] = date;
    row["dateSource"] = era_date_source(d);
    if (d.p_ai) row["pAi"] = round3(*d.p_ai);
    else row["pAi"] = nullptr;
    row["band"] = d.band;
    return row;
}

static const char* SCORE_METHOD =
    "Each document's p_ai is a logistic blend of local stylometry, stock-phrase, sentence-uniformity, "
    "and n-gram-repetition detectors scored against pre-2019 writing in the same visible collection "
    "(or the same topic when a topic is selected), plus embedding distance from that pre-2019 centroid. "
    "Pages dated before 2019 are the human baseline and stay near zero. A small rank mix spreads later "
    "scores. The collection percent is the mean of those document scores.";

static std::string fmt_num(double v, int digits = 2) {
    char buf[32];
    if (digits <= 0) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (digits == 1) std::snprintf(buf, sizeof(buf), "%.1f", v);
    else std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

static std::string fmt_times(double after, double before) {
    if (before < 1e-4 && after < 1e-4) return "about the same as";
    if (before < 1e-4) return "much higher than";
    double r = after / before;
    if (r >= 1.08) return fmt_num(r, 1) + "x as high as";
    if (r <= 0.92) return fmt_num(r * 100, 0) + "% as high as";
    return "about the same as";
}

static std::string fmt_delta_pct(double after, double before) {
    if (std::abs(before) < 1e-4) {
        if (after > before + 0.02) return "rose from near zero";
        if (after + 0.02 < before) return "fell toward zero";
        return "barely moved";
    }
    double rel = (after - before) / std::abs(before);
    if (rel >= 0.03) return "rose " + fmt_num(rel * 100, 0) + "%";
    if (rel <= -0.03) return "fell " + fmt_num(-rel * 100, 0) + "%";
    return "changed little";
}

static const EraTrait* find_trait(const std::vector<EraTrait>& traits, const std::string& key) {
    for (const auto& t : traits) if (t.key == key) return &t;
    return nullptr;
}

static json mean_signals_json(const std::vector<Document>& docs) {
    json row = view_signals_json(docs);
    row["sentenceLengthVariance"] = round3(mean_opt(docs, &Document::sentence_length_std));
    row["burstiness"] = round3(mean_opt(docs, &Document::burstiness));
    return row;
}

static std::string map_headline_copy(const std::string& topic, const std::vector<Document>& docs) {
    int share = pct(mean_opt(docs, &Document::p_ai));
    std::ostringstream out;
    if (!topic.empty()) out << "On " << topic << ", ";
    out << "the " << share << "% figure is the mean of document p_ai scores, the share of documents, not a count of proven AI pages. ";
    out << "Underneath that mean: stylometry " << fmt_num(mean_opt(docs, &Document::detector_stylometry))
        << ", stock phrases " << fmt_num(mean_stock(docs))
        << ", sentence uniformity " << fmt_num(mean_opt(docs, &Document::detector_uniformity))
        << ", n-gram repetition " << fmt_num(mean_opt(docs, &Document::detector_repetition))
        << ", embedding anomaly vs the pre-2019 centroid "
        << fmt_num(mean_opt(docs, &Document::embedding_anomaly))
        << ". Those signals go through the logistic blend above.";
    return out.str();
}

static std::string why_traits_copy(const std::string& topic, const std::vector<EraTrait>& traits,
                                  double mean_before, double mean_after) {
    std::ostringstream out;
    int pre = pct(mean_before);
    int post = pct(mean_after);
    out << "On " << topic << ", the 50 pages created in 2019 or later have a mean AI-likelihood estimate of "
        << post << "%, compared with " << pre << "% for the 50 pages created before 2019. ";
    if (mean_after > mean_before + 0.02) {
        out << "The later cohort is called more AI-like because that mean is higher. ";
    } else if (mean_before > mean_after + 0.02) {
        out << "In this sample the later cohort is not higher on the overall estimate; the detectors still show how the two groups differ. ";
    } else {
        out << "The two cohorts are close on the overall estimate; the detectors show the mix underneath. ";
    }

    const EraTrait* ngram = find_trait(traits, "ngram_repetition");
    const EraTrait* stock = find_trait(traits, "stock_phrases");
    const EraTrait* uni = find_trait(traits, "sentence_uniformity");
    const EraTrait* var = find_trait(traits, "sentence_length_variance");
    const EraTrait* burst = find_trait(traits, "burstiness");
    const EraTrait* cent = find_trait(traits, "pre2019_centroid_distance");
    if (ngram) {
        out << "After 2019, these pages repeat n-grams " << fmt_times(ngram->after, ngram->before)
            << " the 50 pre-2019 pages in the same topic (" << fmt_num(ngram->after) << " vs "
            << fmt_num(ngram->before) << "). ";
    }
    if (stock) {
        out << "Stock phrases are " << fmt_times(stock->after, stock->before)
            << " that pre-2019 sample (" << fmt_num(stock->after) << " vs " << fmt_num(stock->before) << "). ";
    }
    if (uni) {
        out << "Sentence uniformity " << fmt_delta_pct(uni->after, uni->before)
            << " (" << fmt_num(uni->after) << " vs " << fmt_num(uni->before) << "). ";
    }
    if (var) {
        out << "Sentence-length variance " << fmt_delta_pct(var->after, var->before)
            << " (" << fmt_num(var->after) << " vs " << fmt_num(var->before) << "). ";
    }
    if (burst) {
        out << "Burstiness " << fmt_delta_pct(burst->after, burst->before)
            << " (" << fmt_num(burst->after) << " vs " << fmt_num(burst->before) << "). ";
    }
    if (cent) {
        out << "Embedding distance from the pre-2019 centroid " << fmt_delta_pct(cent->after, cent->before)
            << " (" << fmt_num(cent->after) << " vs " << fmt_num(cent->before) << "). ";
    }
    out << SCORE_METHOD;
    return out.str();
}

static std::string why_short_copy(const std::string& topic, const std::vector<EraTrait>& traits,
                                 double mean_before, double mean_after) {
    const EraTrait* ngram = find_trait(traits, "ngram_repetition");
    const EraTrait* stock = find_trait(traits, "stock_phrases");
    std::ostringstream out;
    out << topic << " " << pct(mean_after) << "% after 2019 vs " << pct(mean_before) << "% before";
    if (ngram) {
        out << "; n-grams " << fmt_times(ngram->after, ngram->before) << " the pre-2019 sample";
    }
    if (stock) {
        out << "; stock phrases " << fmt_times(stock->after, stock->before) << " that sample";
    }
    out << ". Mean of document scores; ESTIMATE, not proof.";
    return out.str();
}

static std::string why_no_cohort(const std::string& topic, int n, int dated, int pre, int post,
                                const std::vector<Document>& members) {
    std::ostringstream out;
    out << topic << " cannot fill 50 pages created before 2019 and 50 created in 2019 or later ("
        << pre << " dated before 2019, " << post << " dated 2019 or later, " << (n - dated)
        << " still undated of " << n << "). ";
    out << map_headline_copy(topic, members);
    return out.str();
}

static json trait_json(const EraTrait& t) {
    json row;
    row["key"] = t.key;
    row["label"] = t.label;
    row["before"] = round3(t.before);
    row["after"] = round3(t.after);
    row["higherIsAi"] = t.higher_is_ai;
    row["aiLike"] = trait_ai_like(t);
    return row;
}

nlohmann::json era_traits_json(Store& store, int64_t dataset_id, const std::string& topic) {
    if (is_union_scope(dataset_id)) {
        json out;
        out["datasetId"] = "all";
        out["union"] = true;
        out["available"] = false;
        out["topics"] = json::array();
        out["reason"] = "Combined All sources uses the same document p_ai blend; collection share is the mean over the visible union.";
        auto docs = docs_for_view(store, DATASET_ALL, topic, {});
        apply_era_scores(docs, Config{}, true);
        out["collectionSignals"] = view_signals_json(docs);
        return out;
    }
    auto ds = store.get_dataset(dataset_id);
    if (!ds) throw std::runtime_error("dataset not found");
    json out;
    out["datasetId"] = ds->id;
    out["splitYear"] = ERA_SPLIT_YEAR;
    out["cohortSize"] = ERA_COHORT_SIZE;
    out["minTopicDocuments"] = MIN_TOPIC_DOCUMENTS;
    out["dateField"] = "createdAt";
    out["dateNote"] =
        "Dates use first-revision / createdAt when present, otherwise publishedAt.";
    out["disclaimer"] =
        "This is an estimate from local stylometry and detectors, not proof of authorship.";
    out["scoreMethod"] = SCORE_METHOD;
    bool wiki = ds->kind == KIND_WIKI || is_wikipedia_name(ds->name);
    out["available"] = wiki;
    if (!wiki) {
        out["reason"] = "This 50/50 page-creation comparison is for Wikipedia topics in this collection only.";
        out["topics"] = json::array();
        return out;
    }

    auto docs = store.docs_by_dataset(dataset_id, false);
    if (wiki) remap_wiki_topics(docs);
    std::string wanted = trim(topic);
    if (!wanted.empty() && wiki) wanted = display_wiki_topic(wanted, wanted);

    std::vector<Document> scoped;
    std::map<std::string, std::vector<Document>> by_topic;
    for (const auto& d : docs) {
        if (is_blank(d.topic)) continue;
        if (!wanted.empty() && ascii_lower(d.topic) != ascii_lower(wanted)) continue;
        by_topic[d.topic].push_back(d);
        scoped.push_back(d);
    }
    std::vector<Document> signal_docs = scoped.empty() ? docs : scoped;
    apply_era_scores(signal_docs, Config{}, true);
    out["collectionSignals"] = mean_signals_json(signal_docs);
    out["headlineWhy"] = map_headline_copy(wanted, signal_docs);

    json topics = json::array();
    int waiting = 0;
    for (auto& [name, members] : by_topic) {
        if (static_cast<int>(members.size()) < MIN_TOPIC_DOCUMENTS) continue;
        std::vector<Document> before;
        std::vector<Document> after;
        int dated = 0;
        for (const auto& d : members) {
            if (!has_created_date(d)) continue;
            dated++;
            if (is_pre_2019(d)) before.push_back(d);
            else if (is_post_2019(d)) after.push_back(d);
        }
        json row;
        row["topic"] = name;
        row["documentCount"] = members.size();
        row["datedCount"] = dated;
        row["beforePool"] = before.size();
        row["afterPool"] = after.size();
        row["collectionSignals"] = mean_signals_json(members);
        if (static_cast<int>(before.size()) < ERA_COHORT_SIZE ||
            static_cast<int>(after.size()) < ERA_COHORT_SIZE) {
            waiting++;
            row["cohortReady"] = false;
            row["beforeSample"] = 0;
            row["afterSample"] = 0;
            const std::string why = why_no_cohort(name, static_cast<int>(members.size()), dated,
                                                 static_cast<int>(before.size()),
                                                 static_cast<int>(after.size()), members);
            row["why"] = why;
            row["whyShort"] = why;
            row["traits"] = json::array();
            row["highlightedTraits"] = json::array();
            topics.push_back(row);
            continue;
        }
        auto pre = sample_evenly(before, ERA_COHORT_SIZE);
        auto post = sample_evenly(after, ERA_COHORT_SIZE);

        std::vector<std::vector<float>> pre_vecs;
        for (const auto& d : pre) {
            if (!d.embedding.empty()) pre_vecs.push_back(d.embedding);
        }
        auto pre_cent = centroid(pre_vecs);

        double p_before = mean_opt(pre, &Document::p_ai);
        double p_after = mean_opt(post, &Document::p_ai);
        std::vector<EraTrait> traits = {
            {"p_ai", "mean AI-likelihood estimate", p_before, p_after, true},
            {"stock_phrases", "stock phrases", mean_stock(pre), mean_stock(post), true},
            {"sentence_uniformity", "sentence uniformity",
             mean_opt(pre, &Document::detector_uniformity),
             mean_opt(post, &Document::detector_uniformity), true},
            {"ngram_repetition", "n-gram repetition",
             mean_opt(pre, &Document::detector_repetition),
             mean_opt(post, &Document::detector_repetition), true},
            {"embedding_anomaly", "embedding anomaly",
             mean_opt(pre, &Document::embedding_anomaly),
             mean_opt(post, &Document::embedding_anomaly), true},
            {"stylometry", "stylometry (formulaic / low-burstiness marks)",
             mean_opt(pre, &Document::detector_stylometry),
             mean_opt(post, &Document::detector_stylometry), true},
            {"pre2019_centroid_distance", "embedding distance from the pre-2019 centroid",
             mean_pre_centroid_dist(pre, pre_cent), mean_pre_centroid_dist(post, pre_cent), true},
            {"sentence_length_variance", "sentence-length variance",
             mean_opt(pre, &Document::sentence_length_std),
             mean_opt(post, &Document::sentence_length_std), false},
            {"burstiness", "sentence-length burstiness",
             mean_opt(pre, &Document::burstiness),
             mean_opt(post, &Document::burstiness), false},
        };

        json trait_rows = json::array();
        json highlighted = json::array();
        for (const auto& t : traits) {
            trait_rows.push_back(trait_json(t));
            if (trait_ai_like(t)) highlighted.push_back(t.label);
        }

        row["cohortReady"] = true;
        row["beforeSample"] = pre.size();
        row["afterSample"] = post.size();
        row["why"] = why_traits_copy(name, traits, p_before, p_after);
        row["whyShort"] = why_short_copy(name, traits, p_before, p_after);
        row["traits"] = trait_rows;
        row["highlightedTraits"] = highlighted;
        json before_json = json::array();
        json after_json = json::array();
        for (const auto& d : pre) before_json.push_back(era_article_json(d));
        for (const auto& d : post) after_json.push_back(era_article_json(d));
        row["before"] = before_json;
        row["after"] = after_json;
        row["signals"] = {
            {"before", {{"pAi", round3(p_before)}, {"uniformity", round3(mean_opt(pre, &Document::detector_uniformity))},
                        {"stockPhrases", round3(mean_stock(pre))}, {"ngrams", round3(mean_opt(pre, &Document::detector_repetition))},
                        {"embeddingAnomaly", round3(mean_opt(pre, &Document::embedding_anomaly))}}},
            {"after", {{"pAi", round3(p_after)}, {"uniformity", round3(mean_opt(post, &Document::detector_uniformity))},
                       {"stockPhrases", round3(mean_stock(post))}, {"ngrams", round3(mean_opt(post, &Document::detector_repetition))},
                       {"embeddingAnomaly", round3(mean_opt(post, &Document::embedding_anomaly))}}}
        };
        row["sampling"] =
            "Evenly spaced 50 from pages created before 2019-01-01 and 50 from pages created on or after that date, "
            "sorted by first-revision date. Embedding distance uses the centroid of the 50 pre-2019 pages only.";
        topics.push_back(row);
    }
    out["topics"] = topics;
    out["waitingTopicCount"] = waiting;
    if (topics.empty()) {
        if (!wanted.empty()) {
            out["reason"] =
                "This topic has fewer than 100 pages, so it is not listed. The collection figure is still the mean "
                "of document p_ai scores from stylometry, detectors, and embedding distance. ESTIMATE, not proof.";
        } else {
            out["reason"] =
                "No Wikipedia topic in this collection yet has 100 articles. The collection figure is still the mean "
                "of document p_ai scores from stylometry, detectors, and embedding distance. ESTIMATE, not proof.";
        }
        if (out["headlineWhy"].is_string() && !out["headlineWhy"].get<std::string>().empty()) {
            out["reason"] = out["reason"].get<std::string>() + " " + out["headlineWhy"].get<std::string>();
        }
    }
    return out;
}

nlohmann::json era_cohorts_json(Store& store, int64_t dataset_id, const std::string& topic) {
    return era_traits_json(store, dataset_id, topic);
}

}  // namespace kos
