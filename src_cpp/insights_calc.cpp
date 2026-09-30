#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "analysis.hpp"
#include "config.hpp"
#include "util.hpp"

#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

#include "insights_internal.hpp"

namespace kos {
namespace {

static std::string fmt_fixed(double v, int digits) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << v;
    return os.str();
}

static std::string json_num(const json& v, int digits = 3) {
    if (v.is_null() || v.is_discarded()) return "n/a";
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    if (v.is_number_float()) return fmt_fixed(v.get<double>(), digits);
    return v.dump();
}

static int json_int(const json& v, int fallback = 0) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return static_cast<int>(std::lround(v.get<double>()));
    return fallback;
}

static std::optional<int> json_opt_int(const json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key) || obj[key].is_null()) return std::nullopt;
    if (obj[key].is_number_integer()) return obj[key].get<int>();
    if (obj[key].is_number()) return static_cast<int>(std::lround(obj[key].get<double>()));
    return std::nullopt;
}

static std::string graph_group_plain(const std::string& group_by) {
    if (group_by == "topic") return "country heading (stored topic label)";
    if (group_by == "dataset" || group_by == "source") return "collection";
    if (group_by == "band") return "estimate band";
    return "heading";
}

static json make_step(int n, const std::string& id, const std::string& title, const std::string& formula,
                      const json& inputs, const json& intermediates, const std::string& result,
                      const std::string& decision) {
    json step;
    step["n"] = n;
    step["id"] = id;
    step["title"] = title;
    step["formula"] = formula;
    step["inputs"] = inputs;
    step["intermediates"] = intermediates;
    step["result"] = result;
    step["decision"] = decision;
    return step;
}

static void write_step_prose(std::ostringstream& out, const json& step) {
    out << step.value("n", 0) << ". " << step.value("title", std::string());
    std::string formula = step.value("formula", std::string());
    if (!formula.empty()) out << "\n   Formula: " << formula;
    const json& inputs = step["inputs"];
    if (inputs.is_object() && !inputs.empty()) {
        out << "\n   Inputs: ";
        bool first = true;
        for (auto it = inputs.begin(); it != inputs.end(); ++it) {
            if (!first) out << "; ";
            first = false;
            out << it.key() << " = " << (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
        }
    }
    const json& mid = step["intermediates"];
    if (mid.is_object() && !mid.empty()) {
        out << "\n   Intermediate: ";
        bool first = true;
        for (auto it = mid.begin(); it != mid.end(); ++it) {
            if (!first) out << "; ";
            first = false;
            out << it.key() << " = " << (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
        }
    }
    std::string result = step.value("result", std::string());
    if (!result.empty()) out << "\n   Result: " << result;
    std::string decision = step.value("decision", std::string());
    if (!decision.empty()) out << "\n   Decision rule: " << decision;
}

static json build_calculations(const json& payload) {
    const json& summary = payload.contains("summary") && payload["summary"].is_object()
                              ? payload["summary"]
                              : payload;
    json graph = payload.value("graph", json::object());
    json time = payload.value("time", json::object());
    Config cfg = Config::from_env();

    int n = json_int(summary.value("documentCount", 0));
    int corpus = json_int(summary.value("corpusDocumentCount", n), n);
    json bands = summary.value("bands", json::object());
    int ai = json_int(bands.value(BAND_AI, 0));
    int human = json_int(bands.value(BAND_HUMAN, 0));
    int unc = json_int(bands.value(BAND_UNC, 0));
    int scored = ai + human + unc;
    int pending = n > scored ? n - scored : 0;

    json signals = summary.value("collectionSignals", json::object());
    std::optional<int> share_docs = json_opt_int(summary, "shareOfDocuments");
    std::optional<int> share_words = json_opt_int(summary, "shareOfAnalyzedWords");
    json doc_range = summary.value("shareOfDocumentsRange", json::array());
    json word_range = summary.value("shareOfAnalyzedWordsRange", json::array());
    int words = json_int(summary.value("analyzedWordCount", 0));
    json mean_p = signals.contains("pAi") ? signals["pAi"] : json(nullptr);
    double mean_p_val = mean_p.is_number() ? mean_p.get<double>()
                                           : (share_docs ? *share_docs / 100.0 : 0.0);

    json steps = json::array();
    int step_n = 0;

    json count_inputs = {
        {"visibleDocuments", n},
        {"collectionDocuments", corpus},
        {"likelyAiGenerated", ai},
        {"likelyHumanWritten", human},
        {"uncertain", unc}
    };
    json count_mid = {{"scoredDocuments", scored}, {"pendingDocuments", pending}};
    std::string count_result = std::to_string(n) + " visible document" + (n == 1 ? "" : "s") + ", " +
                               std::to_string(scored) + " scored, " + std::to_string(pending) + " pending";
    steps.push_back(make_step(
        ++step_n, "counts", "What we counted",
        "pending = visible documents - scored documents. A page is scored when it already has an AI-likelihood estimate p_ai and a band.",
        count_inputs, count_mid, count_result,
        "Only scored pages enter the collection percent. Pending pages are omitted from the mean."));

    if (share_docs) {
        json share_in = {
            {"scoredDocuments", scored},
            {"meanPAi", mean_p.is_number() ? json(round3(mean_p_val)) : json(nullptr)},
            {"storedShareOfDocumentsPercent", *share_docs}
        };
        json share_mid = json::object();
        if (mean_p.is_number()) {
            share_mid["100TimesMeanPAi"] = round3(mean_p_val * 100.0);
        }
        share_mid["roundedPercent"] = *share_docs;
        std::string share_result = std::to_string(*share_docs) + "% of documents";
        steps.push_back(make_step(
            ++step_n, "documentShare", "Headline AI share of documents",
            "share% = round(100 × mean of p_ai over scored documents). p_ai is already stored per page (logistic blend of stylometry, template phrases, sentence-length variance, repeated phrases, and embedding distance from the pre-2019 writing centroid in this visible set). Pages dated before 2019 stay near zero.",
            share_in, share_mid, share_result,
            "This percent is the mean of document scores — the share of documents — not a count of proven AI pages. GraphSAGE is not used in this share."));
    } else {
        steps.push_back(make_step(
            ++step_n, "documentShare", "Headline AI share of documents",
            "share% = round(100 × mean of p_ai over scored documents).",
            {{"scoredDocuments", scored}}, json::object(),
            summary.value("headline", std::string("This visible set is not yet analyzed.")),
            "No stored p_ai scores, so a collection percent cannot be formed. GraphSAGE is not used in this share."));
    }

    if (share_docs && doc_range.is_array() && doc_range.size() >= 2) {
        int lo = json_int(doc_range[0]);
        int hi = json_int(doc_range[1]);
        json range_in = {
            {"scoredDocuments", scored},
            {"meanPAi", mean_p.is_number() ? json(round3(mean_p_val)) : json(nullptr)},
            {"storedRangePercent", json::array({lo, hi})}
        };
        steps.push_back(make_step(
            ++step_n, "documentRange", "Uncertainty range for the document share",
            "SE = s / √n, where s is the sample standard deviation of p_ai. If n < 2, SE is taken as 0.08. Interval = [clip01(mean - 1.96×SE), clip01(mean + 1.96×SE)], then each end × 100 and rounded.",
            range_in, json::object(),
            "range " + std::to_string(lo) + "–" + std::to_string(hi) + "%",
            "The stored range is that interval. It is a conventional 95% interval around the mean of the scores, not a claim that authorship is proven inside the band."));
    }

    if (share_words) {
        json word_in = {
            {"scoredDocuments", scored},
            {"analyzedWordCount", words},
            {"storedShareOfAnalyzedWordsPercent", *share_words}
        };
        if (word_range.is_array() && word_range.size() >= 2) {
            word_in["storedRangePercent"] = json::array({json_int(word_range[0]), json_int(word_range[1])});
        }
        std::string word_result = std::to_string(*share_words) + "% of analyzed words";
        if (word_range.is_array() && word_range.size() >= 2) {
            word_result += " (range " + std::to_string(json_int(word_range[0])) + "–" +
                           std::to_string(json_int(word_range[1])) + "%)";
        }
        steps.push_back(make_step(
            ++step_n, "wordShare", "AI share of analyzed words",
            "word share = Σ(p_ai × word_count) / Σ(word_count). The stored word range uses the same SE as the document scores (not a separate word-weighted SE), then clip01(mean ± 1.96×SE) × 100.",
            word_in, json::object(), word_result,
            "This is a word-weighted mean of the same stored p_ai scores. GraphSAGE is not used here either."));
    }

    json band_in = {
        {"likelyAiMinPAi", cfg.band_ai_min},
        {"likelyHumanMaxPAi", cfg.band_human_max},
        {"uncertainIfIntervalWiderThan", cfg.band_max_interval},
        {"likelyAiGeneratedCount", ai},
        {"likelyHumanWrittenCount", human},
        {"uncertainCount", unc}
    };
    json band_mid = {
        {"likelyAiMinPercent", pct(cfg.band_ai_min)},
        {"likelyHumanMaxPercent", pct(cfg.band_human_max)}
    };
    std::string band_result = std::to_string(ai) + " likely AI-generated, " + std::to_string(human) +
                              " likely human-written, " + std::to_string(unc) + " uncertain";
    steps.push_back(make_step(
        ++step_n, "bands", "How each page gets a band",
        "If (ci_high - ci_low) > max interval, the band is uncertain. Else if p_ai ≥ likely-AI cutoff → likely AI-generated. Else if p_ai ≤ likely-human cutoff → likely human-written. Else uncertain.",
        band_in, band_mid, band_result,
        "Bands classify each stored score against those cutoffs. They are labels on the same p_ai values, not a second model."));

    json rows = time.value("rows", json::array());
    bool time_ok = time.value("available", false) && rows.is_array() && !rows.empty();
    int dated_bins = 0;
    json examples = json::array();
    json first_bin, last_bin, peak_bin;
    if (rows.is_array()) {
        for (const auto& r : rows) {
            if (!r.is_object() || r.value("estimatePercent", json(nullptr)).is_null()) continue;
            dated_bins++;
            if (first_bin.is_null()) first_bin = r;
            last_bin = r;
            if (peak_bin.is_null() || json_int(r.value("estimatePercent", 0)) > json_int(peak_bin.value("estimatePercent", 0))) {
                peak_bin = r;
            }
        }
    }
    if (!first_bin.is_null()) examples.push_back(first_bin);
    if (!last_bin.is_null() && last_bin.value("key", std::string()) != first_bin.value("key", std::string())) {
        examples.push_back(last_bin);
    }
    if (!peak_bin.is_null()) {
        std::string pk = peak_bin.value("key", std::string());
        if (pk != first_bin.value("key", std::string()) && pk != last_bin.value("key", std::string())) {
            examples.push_back(peak_bin);
        }
    }
    json time_in = {
        {"yearBinStartsAt", TIME_CHART_MIN_YEAR},
        {"dateRule", "calendar year of first-revision / created date when stored, otherwise published date"},
        {"yearBinCount", time_ok ? static_cast<int>(rows.size()) : 0},
        {"binsWithAMean", dated_bins},
        {"exampleBins", examples}
    };
    std::string time_result;
    if (!time_ok) {
        time_result = "No dated pages in this visible set, so a yearly series cannot be drawn.";
    } else {
        time_result = std::to_string(rows.size()) + " year-bin" + (rows.size() == 1 ? "" : "s") +
                      " from " + std::to_string(TIME_CHART_MIN_YEAR) + " through the latest dated year";
        if (!last_bin.is_null() && last_bin.contains("key")) {
            time_result += ". Latest bin " + last_bin.value("key", std::string()) + " = " +
                           json_num(last_bin.value("estimatePercent", json(nullptr)), 0) + "%";
            if (last_bin.contains("documentCount")) {
                time_result += " on " + json_num(last_bin["documentCount"], 0) + " page(s)";
            }
        }
    }
    steps.push_back(make_step(
        ++step_n, "time", "Yearly series",
        "A year-bin is the calendar year of the document date (created / first-revision if present, else published). Years before 2015 are omitted. The estimate for a year is the mean of p_ai in that bin, then × 100 and rounded — the same mean as the headline, restricted to that year. The range in a bin uses the same 1.96×SE rule.",
        time_in, json::object(), time_result,
        "A later date can mean an edit, not a newly written page. Combined All sources unions years from every collection."));

    std::string group_by = graph.value("groupBy", std::string("topic"));
    int nodes = json_int(graph.value("nodeCount", 0));
    int edges = json_int(graph.value("edgeCount", 0));
    json groups = graph.value("groups", json::array());
    json group_names = json::array();
    if (groups.is_array()) {
        size_t shown = 0;
        for (const auto& g : groups) {
            if (!g.is_object()) continue;
            std::string label = g.value("label", g.value("key", std::string()));
            if (label.empty()) continue;
            json row = {{"label", label}, {"count", json_int(g.value("count", 0))}};
            group_names.push_back(row);
            if (++shown >= 4) break;
        }
    }
    json graph_in = {
        {"nodeCount", nodes},
        {"edgeCount", edges},
        {"groupBy", group_by},
        {"groupByPlain", graph_group_plain(group_by)},
        {"nearestNeighbors", cfg.graph_k},
        {"minCosineSimilarity", cfg.graph_min_cosine},
        {"exampleGroups", group_names}
    };
    std::string graph_result = std::to_string(nodes) + " document node" + (nodes == 1 ? "" : "s") + ", " +
                               std::to_string(edges) + " similarity edge" + (edges == 1 ? "" : "s") +
                               ", grouped by " + graph_group_plain(group_by);
    steps.push_back(make_step(
        ++step_n, "graph", "Graph grouping",
        "Nodes are documents. Edges are nearest-neighbor text similarity (up to k neighbors, cosine at least the stored minimum). Group labels come from stored metadata (country or collection), not from a second authorship model.",
        graph_in, json::object(), graph_result,
        "Clicking a heading only filters this same collection. GraphSAGE is not in the headline share."));

    json gs = summary.value("graphsage", json::object());
    bool gs_used = gs.value("usedInHeadline", false);
    steps.push_back(make_step(
        ++step_n, "graphsage", "What is not in the headline",
        "headline share uses only the mean of stored document p_ai scores.",
        {{"graphsageUsedInHeadline", gs_used},
         {"graphsageStatus", gs.value("status", std::string(GS_NOT_TRAINED))}},
        json::object(),
        gs_used ? "GraphSAGE was marked as used — unexpected; the share should still be read as the mean of p_ai."
                : "GraphSAGE is not in this share.",
        "Similarity clustering and GraphSAGE embeddings may appear as graph context. They do not enter the collection percent."));

    std::string name = payload.value("name", summary.value("name", std::string("This collection")));
    bool topic_view = payload.value("topicView", false);
    bool id_view = payload.value("idView", false);
    std::string topic;
    if (payload.contains("topic") && payload["topic"].is_string()) topic = payload["topic"].get<std::string>();
    std::ostringstream prose;
    prose << name;
    if (id_view) prose << ", restricted to the selected cluster";
    else if (topic_view && !topic.empty()) prose << ", restricted to topic \"" << topic << "\"";
    prose << ". Sample: " << n << " visible document" << (n == 1 ? "" : "s") << ", " << scored << " scored";
    if (pending) prose << ", " << pending << " unscored and left out of the mean";
    else prose << ", none left out of the mean";
    if (words) prose << ". Analyzed words: " << words;
    prose << ".";
    if (!share_docs && !share_words) {
        prose << "\n\nNo stored scores, so there is no mean to report.";
    } else {
        prose << "\n\nEstimand. Each document has a stored score p_i in [0, 1]. "
                 "It is a logistic function of stylometry, stock-phrase rate, sentence-length uniformity, "
                 "n-gram repetition, and embedding distance from the pre-2019 centroid of this visible set, "
                 "plus a small rank mix. It is not a posterior probability that a model wrote the page. "
                 "Documents whose stored year is before 2019 are pinned near 0 and labeled human; "
                 "they are the baseline, so they pull the mean down by construction. "
                 "GraphSAGE does not enter p_i.";
        if (share_docs) {
            prose << "\n\nUnweighted mean. mean(p) = " << round3(mean_p_val)
                  << ", reported as " << *share_docs << "% of documents.";
            if (doc_range.is_array() && doc_range.size() >= 2) {
                prose << " Interval: " << json_int(doc_range[0]) << "-" << json_int(doc_range[1]) << "%.";
            }
            prose << " That interval is mean(p) +/- 1.96 s/sqrt(n), where s is the sample standard deviation of the p_i "
                     "(if n < 2, the SE is replaced by 0.08). Ends are clipped to [0, 1], multiplied by 100, and rounded. "
                     "The scores share a centroid and the same detectors, so they are not independent. "
                     "Read it as a Wald interval around the mean of these scores, not a confidence interval for a population authorship rate.";
        }
        if (share_words) {
            prose << "\n\nWord-weighted mean. sum(p_i * w_i) / sum(w_i) = " << *share_words << "%";
            if (words) prose << " on " << words << " words";
            prose << ".";
            if (word_range.is_array() && word_range.size() >= 2) {
                prose << " The interval printed with it (" << json_int(word_range[0]) << "-"
                      << json_int(word_range[1])
                      << "%) reuses the unweighted SE of the document scores.";
            }
            prose << " That is not a standard error for the weighted mean.";
        }
        prose << "\n\nLabels. Same p_i, fixed cutoffs, not a second model. "
                 "Uncertain if the document's own interval is wider than " << cfg.band_max_interval << ". "
                 "Otherwise likely AI-generated if p_i >= " << cfg.band_ai_min
              << ", likely human-written if p_i <= " << cfg.band_human_max << ", else uncertain. "
                 "Counts: " << ai << " likely AI-generated, " << human << " likely human-written, "
              << unc << " uncertain.";
        if (scored && share_docs) {
            prose << " The count above the AI cutoff is " << ai << "/" << scored
                  << ", which is not the " << *share_docs << "% mean.";
        }
        if (!time_ok) {
            prose << "\n\nYear strata. No dated documents in this visible set. "
                     "A year is the calendar year of createdAt when that date is stored (Wikipedia first revision), otherwise publishedAt. "
                     "Years before 2015 are dropped.";
        } else {
            prose << "\n\nYear strata. " << rows.size() << " year-bin" << (rows.size() == 1 ? "" : "s");
            if (!first_bin.is_null() && !last_bin.is_null()) {
                prose << " (" << first_bin.value("key", std::string()) << "-"
                      << last_bin.value("key", std::string()) << ")";
            }
            prose << "; " << dated_bins << " have a mean. "
                     "The bin is the calendar year of createdAt when stored (Wikipedia first revision), otherwise publishedAt. "
                     "Years before 2015 are omitted. Within a year the estimator is the same unweighted mean. ";
            if (!last_bin.is_null() && last_bin.contains("estimatePercent") && !last_bin["estimatePercent"].is_null()) {
                prose << "Latest bin " << last_bin.value("key", std::string()) << ": "
                      << json_num(last_bin.value("estimatePercent", json(nullptr)), 0) << "% on n = "
                      << json_int(last_bin.value("documentCount", 0));
                json lr = last_bin.value("rangePercent", json::array());
                if (lr.is_array() && lr.size() >= 2) {
                    prose << ", interval " << json_int(lr[0]) << "-" << json_int(lr[1]) << "%";
                }
                prose << ". ";
            }
            prose << "A later year is a stored date, not the year the current prose was written.";
        }
        prose << "\n\nSimilarity graph. k-nearest neighbors, k = " << cfg.graph_k
              << ", cosine at least " << cfg.graph_min_cosine << ": " << nodes << " nodes, " << edges
              << " edges. Group labels are " << graph_group_plain(group_by);
        if (group_names.is_array() && !group_names.empty()) {
            prose << " (";
            bool first_g = true;
            for (const auto& g : group_names) {
                if (!g.is_object()) continue;
                if (!first_g) prose << "; ";
                first_g = false;
                prose << g.value("label", std::string());
                if (g.contains("count")) prose << " (" << json_int(g["count"]) << ")";
            }
            prose << ")";
        }
        prose << ", not a fitted clustering and not an authorship model.";
    }

    json out;
    out["title"] = "How this share was estimated";
    out["steps"] = steps;
    out["prose"] = prose.str();
    out["thresholds"] = {
        {"likelyAiMinPAi", cfg.band_ai_min},
        {"likelyHumanMaxPAi", cfg.band_human_max},
        {"uncertainIfIntervalWiderThan", cfg.band_max_interval},
        {"timeChartMinYear", TIME_CHART_MIN_YEAR},
        {"graphNearestNeighbors", cfg.graph_k},
        {"graphMinCosine", cfg.graph_min_cosine}
    };
    out["counts"] = {
        {"visibleDocuments", n},
        {"collectionDocuments", corpus},
        {"scoredDocuments", scored},
        {"pendingDocuments", pending},
        {"likelyAiGenerated", ai},
        {"likelyHumanWritten", human},
        {"uncertain", unc},
        {"analyzedWordCount", words}
    };
    if (share_docs) out["shareOfDocuments"] = *share_docs;
    else out["shareOfDocuments"] = nullptr;
    if (share_words) out["shareOfAnalyzedWords"] = *share_words;
    else out["shareOfAnalyzedWords"] = nullptr;
    out["shareOfDocumentsRange"] = doc_range;
    out["shareOfAnalyzedWordsRange"] = word_range;
    out["meanPAi"] = mean_p.is_number() ? json(round3(mean_p_val)) : json(nullptr);
    out["graphsageUsedInHeadline"] = gs_used;
    return out;
}

}  // namespace

namespace insights_detail {

void attach_calculations(json& result, const json& payload) {
    try {
        json calcs = build_calculations(payload);
        result["calculations"] = calcs;
        result["howCalculated"] = calcs.value("prose", std::string());
    } catch (...) {
        result["calculations"] = json::object();
        result["howCalculated"] =
            "The stored summary numbers could not be turned into a calculation trail.";
    }
}

json parse_agent_stdout(const std::string& raw, const json& payload) {
    std::string t = trim(raw);
    json out;
    out["llmRequired"] = false;
    if (!t.empty() && t.front() == '{') {
        try {
            auto parsed = json::parse(t);
            if (parsed.is_object() && parsed.contains("text") && parsed["text"].is_string()) {
                out["text"] = parsed["text"].get<std::string>();
                out["source"] = parsed.value("source", "agent");
                out["llmRequired"] = false;
                if (parsed.contains("calculations")) out["calculations"] = parsed["calculations"];
                if (parsed.contains("howCalculated") && parsed["howCalculated"].is_string()) {
                    out["howCalculated"] = parsed["howCalculated"].get<std::string>();
                }
                return out;
            }
        } catch (...) {
        }
    }
    if (!t.empty()) {
        out["text"] = t;
        out["source"] = "agent";
        return out;
    }
    out["text"] = fallback_insights(payload);
    out["source"] = "fallback";
    return out;
}

json slim_graph(const json& graph) {
    json slim;
    slim["groupBy"] = graph.value("groupBy", "topic");
    slim["groups"] = graph.value("groups", json::array());
    slim["nodeCount"] = graph.contains("nodes") && graph["nodes"].is_array() ? graph["nodes"].size() : 0;
    slim["edgeCount"] = graph.contains("edges") && graph["edges"].is_array() ? graph["edges"].size() : 0;
    return slim;
}

}  // namespace insights_detail
}  // namespace kos
