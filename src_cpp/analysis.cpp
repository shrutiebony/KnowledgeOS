#include "analysis.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <map>
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

static void assign_scores(std::vector<Document>& docs, const Config& cfg) {
    std::vector<std::vector<float>> vectors;
    std::vector<Features> features;
    std::vector<double> style_ai;
    for (auto& doc : docs) {
        Features f = stylometry_analyze(doc.text);
        features.push_back(f);
        style_ai.push_back(stylometry_ai_score(f));
        doc.word_count = f.word_count;
        doc.type_token_ratio = f.type_token_ratio;
        doc.avg_sentence_length = f.avg_sentence_length;
        doc.sentence_length_std = f.sentence_length_std;
        doc.burstiness = f.burstiness;
        doc.punctuation_ratio = f.punctuation_ratio;
        doc.char_entropy = f.char_entropy;
        doc.repetition_score = f.repetition_score;
        auto emb = hashed_embed(doc.title + "\n" + doc.text, cfg.embed_dim);
        doc.embedding = emb;
        vectors.push_back(emb);
    }

    std::vector<int> order(docs.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return style_ai[a] < style_ai[b]; });
    int baseline_n = std::max(2, std::min(8, static_cast<int>(docs.size()) / 6));
    std::vector<std::vector<float>> human_baseline;
    for (int i = 0; i < std::min(baseline_n, static_cast<int>(order.size())); ++i) {
        human_baseline.push_back(vectors[order[i]]);
    }
    auto cent = centroid(human_baseline.empty() ? vectors : human_baseline);

    std::vector<double> ttr, burst;
    for (const auto& d : docs) {
        ttr.push_back(nz(d.type_token_ratio));
        burst.push_back(nz(d.burstiness));
    }
    Stats ttr_stats = stats_of(ttr);
    Stats burst_stats = stats_of(burst);

    std::vector<double> raw_anomaly(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) {
        raw_anomaly[i] = 1.0 - cosine(docs[i].embedding, cent);
    }
    auto anomaly_rank = ranks01(raw_anomaly);

    std::vector<Estimate> raw_estimates;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        const auto& f = features[i];
        double anomaly = clamp01(0.45 * clamp01(raw_anomaly[i] * 1.4) + 0.55 * anomaly_rank[i]);
        double zt = zscore(f.type_token_ratio, ttr_stats);
        double zb = zscore(f.burstiness, burst_stats);
        double deviation = clamp01((std::abs(zt) + std::abs(zb)) / 6.0);

        std::map<std::string, double> raw;
        raw["stylometry"] = stylometry_ai_score(f);
        for (const auto& det : run_detectors(doc.text, f)) raw[det.name] = det.score;
        raw["embedding_anomaly"] = anomaly;
        raw["stylometry_deviation"] = deviation;
        raw["post_chatgpt"] = post_chatgpt_signal(doc.published_at);

        auto estimate = calibrate(raw, cfg);
        raw_estimates.push_back(estimate);
        doc.detector_stylometry = raw["stylometry"];
        doc.detector_repetition = raw["ngram_repetition_detector"];
        doc.detector_uniformity = raw["sentence_uniformity_detector"];
        doc.embedding_anomaly = anomaly;
        doc.stylometry_deviation = deviation;
    }

    std::vector<double> raw_p;
    for (const auto& e : raw_estimates) raw_p.push_back(e.p_ai);
    auto p_rank = ranks01(raw_p);
    for (size_t i = 0; i < docs.size(); ++i) {
        auto mixed = mix_with_rank(raw_estimates[i], p_rank[i], cfg);
        docs[i].p_ai = mixed.p_ai;
        docs[i].ci_low = mixed.ci_low;
        docs[i].ci_high = mixed.ci_high;
        docs[i].band = mixed.band;
        docs[i].explanation_json = signals_json(mixed.signals);
    }
}

static std::string edge_reason(bool topic, bool source) {
    std::string r = "embedding";
    if (topic) r += "+topic";
    if (source) r += "+source";
    return r;
}

static void rebuild_graph(Store& store, int64_t dataset_id, const std::vector<Document>& docs, const Config& cfg) {
    store.delete_edges(dataset_id);
    std::vector<DocumentEdge> created;
    int k = cfg.graph_k;
    double min_cos = cfg.graph_min_cosine;
    for (size_t i = 0; i < docs.size(); ++i) {
        struct Scored {
            size_t j;
            double score;
            double cos;
            bool topic;
            bool source;
        };
        std::vector<Scored> neighbors;
        const auto& a = docs[i];
        for (size_t j = 0; j < docs.size(); ++j) {
            if (i == j) continue;
            const auto& b = docs[j];
            double cos = cosine(a.embedding, b.embedding);
            bool topic_match = !a.topic.empty() && a.topic == b.topic;
            bool source_match = !a.source.empty() && a.source == b.source;
            double score = cos + (topic_match ? 0.08 : 0) + (source_match ? 0.04 : 0);
            if (cos >= min_cos || topic_match) {
                neighbors.push_back({j, score, cos, topic_match, source_match});
            }
        }
        std::sort(neighbors.begin(), neighbors.end(), [](const Scored& x, const Scored& y) { return x.score > y.score; });
        int limit = std::min(k, static_cast<int>(neighbors.size()));
        for (int n = 0; n < limit; ++n) {
            const auto& s = neighbors[n];
            DocumentEdge e;
            e.dataset_id = dataset_id;
            e.source_document_id = a.id;
            e.target_document_id = docs[s.j].id;
            e.cosine = s.cos;
            e.reason = edge_reason(s.topic, s.source);
            created.push_back(e);
        }
    }
    store.insert_edges(created);
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

std::vector<Document> docs_for_view(Store& store, int64_t dataset_id, const std::string& topic,
                                    const std::vector<int64_t>& ids) {
    auto docs = store.docs_by_dataset(dataset_id, false);
    std::string wanted = trim(topic);
    if (!wanted.empty()) {
        std::string needle = ascii_lower(wanted);
        docs.erase(std::remove_if(docs.begin(), docs.end(), [&](const Document& d) {
            return ascii_lower(trim(d.topic)) != needle;
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

static std::string band_heading(const std::string& band) {
    if (band == BAND_AI) return "likely AI";
    if (band == BAND_HUMAN) return "likely human";
    if (band == BAND_UNC) return "uncertain";
    return "pending";
}

static std::string choose_group_by(const std::vector<Document>& docs, const std::string& topic_filter) {
    std::set<std::string> topics, sources;
    for (const auto& d : docs) {
        if (!is_blank(d.topic)) topics.insert(ascii_lower(d.topic));
        if (!is_blank(d.source)) sources.insert(ascii_lower(d.source));
    }
    if (!trim(topic_filter).empty()) return sources.size() >= 2 ? "source" : "band";
    if (topics.size() >= 2) return "topic";
    if (sources.size() >= 2) return "source";
    return "band";
}

static std::string graph_group_label(const Document& doc, const std::string& group_by) {
    if (group_by == "topic") return is_blank(doc.topic) ? "Uncategorized" : doc.topic;
    if (group_by == "source") return is_blank(doc.source) ? "Unknown source" : doc.source;
    return band_heading(doc.band);
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
    m["topic"] = doc.topic;
    if (doc.published_at.empty()) m["publishedAt"] = nullptr;
    else m["publishedAt"] = doc.published_at;
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
    auto ds = store.get_dataset(dataset_id);
    if (!ds) throw std::runtime_error("dataset not found");
    long corpus = store.count_docs(dataset_id);
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    std::vector<Document> scored;
    for (const auto& d : docs) if (d.p_ai) scored.push_back(d);
    json out;
    out["datasetId"] = ds->id;
    out["name"] = ds->name;
    out["kind"] = ds->kind;
    std::string view_topic = trim(topic);
    if (view_topic.empty()) out["topic"] = nullptr;
    else out["topic"] = view_topic;
    out["topicView"] = !view_topic.empty();
    out["idView"] = !ids.empty();
    out["corpusDocumentCount"] = corpus;
    out["analysisState"] = ds->analysis_state;
    if (ds->last_analyzed_at.empty()) out["lastAnalyzedAt"] = nullptr;
    else out["lastAnalyzedAt"] = ds->last_analyzed_at;
    out["documentCount"] = docs.size();
    out["graphsage"] = {
        {"status", ds->graph_sage_status},
        {"message", ds->graph_sage_message},
        {"usedInHeadline", false}
    };
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
        return out;
    }
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
    if (ds->kind == KIND_URLS) {
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
                          const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    std::string group_by = choose_group_by(docs, topic);
    std::unordered_map<int64_t, std::string> groups_by_id;
    std::map<std::string, int> counts;
    for (const auto& doc : docs) {
        std::string label = graph_group_label(doc, group_by);
        groups_by_id[doc.id] = label;
        counts[label]++;
    }
    if (counts.size() > 8) {
        std::vector<std::pair<std::string, int>> rows(counts.begin(), counts.end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return ascii_lower(a.first) < ascii_lower(b.first);
        });
        std::unordered_set<std::string> keep;
        for (size_t i = 0; i < std::min<size_t>(7, rows.size()); ++i) keep.insert(rows[i].first);
        for (auto& [id, label] : groups_by_id) {
            if (!keep.count(label)) label = "Other";
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
        n["topic"] = doc.topic;
        n["source"] = doc.source;
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
        groups.push_back({{"key", k}, {"label", k}, {"count", c}});
    }
    json links = json::array();
    for (const auto& e : store.edges_by_dataset(dataset_id)) {
        if (!keep.count(e.source_document_id) || !keep.count(e.target_document_id)) continue;
        links.push_back({{"from", e.source_document_id}, {"to", e.target_document_id},
                         {"cosine", round3(e.cosine)}, {"reason", e.reason}});
    }
    json out;
    out["datasetId"] = dataset_id;
    if (trim(topic).empty()) out["topic"] = nullptr;
    else out["topic"] = trim(topic);
    out["idView"] = !ids.empty();
    out["groupBy"] = group_by;
    out["groups"] = groups;
    out["nodes"] = nodes;
    out["edges"] = links;
    return out;
}

static std::vector<Document> spread(const std::vector<Document>& docs, int limit) {
    std::vector<Document> out;
    std::unordered_map<std::string, int> used;
    for (const auto& doc : docs) {
        if (static_cast<int>(out.size()) >= limit) break;
        std::string topic = doc.topic.empty() ? "_" : doc.topic;
        int n = used[topic];
        if (n > 1 && static_cast<int>(out.size()) + 1 < limit) continue;
        used[topic] = n + 1;
        out.push_back(doc);
    }
    for (const auto& doc : docs) {
        if (static_cast<int>(out.size()) >= limit) break;
        bool have = false;
        for (const auto& o : out) if (o.id == doc.id) { have = true; break; }
        if (!have) out.push_back(doc);
    }
    return out;
}

nlohmann::json examples_json(Store& store, int64_t dataset_id, const std::string& band, int limit,
                             const std::string& topic, const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    std::vector<Document> filtered;
    for (const auto& d : docs) if (d.band == band) filtered.push_back(d);
    std::sort(filtered.begin(), filtered.end(), [&](const Document& a, const Document& b) {
        if (band == BAND_AI) return nz(a.p_ai) > nz(b.p_ai);
        if (band == BAND_HUMAN) return nz(a.p_ai) < nz(b.p_ai);
        double ia = (a.ci_high && a.ci_low) ? *a.ci_high - *a.ci_low : 0;
        double ib = (b.ci_high && b.ci_low) ? *b.ci_high - *b.ci_low : 0;
        return ia > ib;
    });
    auto spread_docs = spread(filtered, std::max(1, limit));
    json out = json::array();
    for (const auto& meta : spread_docs) out.push_back(card_json(store, meta, false));
    return out;
}

nlohmann::json breakdown_rows(Store& store, int64_t dataset_id, const std::string& by,
                              const std::string& topic, const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    std::map<std::string, std::vector<Document>> groups;
    for (const auto& doc : docs) {
        if (!doc.p_ai) continue;
        std::string key;
        if (by == "topic") key = trim(doc.topic);
        else if (by == "time") key = doc.published_at.size() >= 4 ? doc.published_at.substr(0, 4) : "";
        else key = trim(doc.source);
        if (key.empty()) continue;
        groups[key].push_back(doc);
    }
    json rows = json::array();
    for (auto& [k, g] : groups) {
        std::vector<double> ps;
        for (const auto& d : g) ps.push_back(*d.p_ai);
        auto iv = mean_interval(ps);
        rows.push_back({{"key", k}, {"documentCount", g.size()}, {"estimatePercent", pct(iv.mean)},
                        {"rangePercent", json::array({pct(iv.low), pct(iv.high)})}});
    }
    return rows;
}

nlohmann::json explanation_json(Store& store, int64_t dataset_id, int64_t doc_id) {
    auto doc = store.get_document(doc_id, true);
    if (!doc) throw std::runtime_error("document not found");
    if (doc->dataset_id != dataset_id) throw std::invalid_argument("Document is not in this dataset");
    json out = card_json(store, *doc, true);
    json neighbors = json::array();
    for (const auto& e : store.edges_from(dataset_id, doc_id)) {
        auto n = store.get_document(e.target_document_id, false);
        if (!n) continue;
        json row = card_json(store, *n, false);
        row["cosine"] = round3(e.cosine);
        row["reason"] = e.reason;
        neighbors.push_back(row);
    }
    out["neighbors"] = neighbors;
    out["disclaimer"] = "This is an estimate from calibrated signals, not proof of authorship.";
    return out;
}

}  // namespace kos
