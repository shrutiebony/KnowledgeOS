#include "analysis.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
        raw["post_chatgpt"] = post_chatgpt_signal(doc.created_at);

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

static void remap_wiki_topics(std::vector<Document>& docs) {
    for (auto& d : docs) d.topic = canonical_wiki_topic(d.topic, d.title);
}

static void remap_wiki_topics_in_union(std::vector<Document>& docs,
                                       const std::unordered_set<int64_t>& wiki_ids) {
    for (auto& d : docs) {
        if (document_is_wikipedia(d, wiki_ids)) d.topic = canonical_wiki_topic(d.topic, d.title);
    }
}

static std::string topic_needle(const std::string& wanted) {
    std::string t = trim(wanted);
    if (t.empty()) return "";
    return ascii_lower(canonical_wiki_topic(t, t));
}

static bool document_matches_topic(const Document& d, const std::string& needle) {
    if (needle.empty()) return true;
    std::string stored = ascii_lower(trim(d.topic));
    if (!stored.empty() && stored == needle) return true;
    std::string canon = ascii_lower(canonical_wiki_topic(d.topic, d.title));
    return canon == needle;
}

static std::string topic_key_for_list(const Document& d, bool canonicalize) {
    if (canonicalize) return canonical_wiki_topic(d.topic, d.title);
    return trim(d.topic);
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
    std::map<std::string, int> counts;
    for (const auto& d : docs) {
        std::string key = topic_key_for_list(d, canonicalize);
        if (is_blank(key)) continue;
        if (canonicalize && is_blank(d.topic) && iequals(key, "General")) continue;
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

static std::string choose_group_by(const std::vector<Document>& docs, const std::string& topic_filter) {
    std::map<std::string, int> topic_counts;
    std::set<std::string> sources;
    for (const auto& d : docs) {
        if (!is_blank(d.topic)) topic_counts[ascii_lower(d.topic)]++;
        if (!is_blank(d.source)) sources.insert(ascii_lower(d.source));
    }
    int substantial_topics = 0;
    for (const auto& [topic, n] : topic_counts) {
        if (n >= MIN_TOPIC_DOCUMENTS) substantial_topics++;
    }
    if (!trim(topic_filter).empty()) return sources.size() >= 2 ? "source" : "band";
    if (substantial_topics >= 1) return "topic";
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
    else out["topic"] = view_topic;
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
    std::string group_by = choose_group_by(docs, topic);
    if (union_view) {
        std::set<std::string> sources;
        for (const auto& d : docs) {
            if (!is_blank(d.source)) sources.insert(ascii_lower(d.source));
        }
        if (sources.size() >= 2) group_by = "source";
    }
    std::unordered_map<int64_t, std::string> groups_by_id;
    std::map<std::string, int> counts;
    for (const auto& doc : docs) {
        std::string label = graph_group_label(doc, group_by);
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
        if (group_by == "topic" && (c < MIN_TOPIC_DOCUMENTS || k == "Other" || k == "Uncategorized")) continue;
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
    else out["topic"] = trim(topic);
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
    std::map<std::string, std::vector<Document>> groups;
    for (const auto& d : docs) {
        std::string key = trim(d.topic);
        if (key.empty() || !d.p_ai) continue;
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

nlohmann::json breakdown_rows(Store& store, int64_t dataset_id, const std::string& by,
                              const std::string& topic, const std::vector<int64_t>& ids) {
    auto docs = docs_for_view(store, dataset_id, topic, ids);
    std::map<std::string, std::vector<Document>> groups;
    for (const auto& doc : docs) {
        if (!doc.p_ai) continue;
        std::string key;
        if (by == "topic") key = trim(doc.topic);
        else if (by == "time") key = doc.created_at.size() >= 4 ? doc.created_at.substr(0, 4) : "";
        else key = trim(doc.source);
        if (key.empty()) continue;
        groups[key].push_back(doc);
    }
    json rows = json::array();
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

static int year_of_date(const std::string& date) {
    if (date.size() < 4) return 0;
    try {
        return std::stoi(date.substr(0, 4));
    } catch (...) {
        return 0;
    }
}

static std::string era_date_of(const Document& d) {
    return d.created_at;
}

static const char* era_date_source(const Document& d) {
    if (!d.created_at.empty() && year_of_date(d.created_at) > 0) return "createdAt";
    return "";
}

static bool has_created_date(const Document& d) {
    return !d.created_at.empty() && year_of_date(d.created_at) > 0;
}

static bool is_pre_2019(const Document& d) {
    return has_created_date(d) && d.created_at < ERA_CUTOFF;
}

static bool is_post_2019(const Document& d) {
    return has_created_date(d) && d.created_at >= ERA_CUTOFF;
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
    "Each document's p_ai is a logistic blend of local stylometry (formulaic / low-burstiness marks), "
    "stock-phrase, sentence-uniformity, and n-gram-repetition detectors, plus embedding distance from a "
    "human-leaning baseline centroid in this collection (the most human-scoring pages by stylometry). "
    "A small rank mix spreads scores across the set. The collection or topic percent is the mean of those "
    "document scores (share of documents). The after-2019 block also reports distance to a pre-2019 centroid; "
    "that distance is a comparison signal, not a second headline. ESTIMATE, not proof of authorship.";

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
    out << "the " << share << "% figure is the mean of document p_ai scores — the share of documents, not a count of proven AI pages. ";
    out << "Underneath that mean: stylometry " << fmt_num(mean_opt(docs, &Document::detector_stylometry))
        << ", stock phrases " << fmt_num(mean_stock(docs))
        << ", sentence uniformity " << fmt_num(mean_opt(docs, &Document::detector_uniformity))
        << ", n-gram repetition " << fmt_num(mean_opt(docs, &Document::detector_repetition))
        << ", embedding anomaly vs the human-leaning centroid "
        << fmt_num(mean_opt(docs, &Document::embedding_anomaly))
        << ". Those signals go through the logistic blend above. ESTIMATE, not proof.";
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
        "Split uses Wikipedia page first-revision / creation date only. Last-modified is not used.";
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
    if (!wanted.empty() && wiki) wanted = canonical_wiki_topic(wanted, wanted);

    std::vector<Document> scoped;
    std::map<std::string, std::vector<Document>> by_topic;
    for (const auto& d : docs) {
        if (is_blank(d.topic)) continue;
        if (!wanted.empty() && ascii_lower(d.topic) != ascii_lower(wanted)) continue;
        by_topic[d.topic].push_back(d);
        scoped.push_back(d);
    }
    out["collectionSignals"] = mean_signals_json(scoped.empty() ? docs : scoped);
    out["headlineWhy"] = map_headline_copy(wanted, scoped.empty() ? docs : scoped);

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
