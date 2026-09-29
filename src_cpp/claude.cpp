#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "claude.hpp"

#include "analysis.hpp"
#include "http_client.hpp"
#include "models.hpp"
#include "util.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

using nlohmann::json;

namespace kos {
namespace {

constexpr const char* UA = "KnowledgeOS/1.0 (claude-adapter)";

std::vector<std::string> tokens_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : ascii_lower(s)) {
        if (std::isalnum(c)) cur.push_back(static_cast<char>(c));
        else if (!cur.empty()) {
            if (cur.size() >= 3) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3) out.push_back(cur);
    return out;
}

std::string excerpt_of(const std::string& text, int max_chars, const std::vector<std::string>& query_tokens) {
    std::string t = text;
    if (static_cast<int>(t.size()) > max_chars * 4) t = t.substr(0, static_cast<size_t>(max_chars * 4));
    size_t best = std::string::npos;
    for (const auto& tok : query_tokens) {
        auto low = ascii_lower(t);
        auto pos = low.find(tok);
        if (pos != std::string::npos && (best == std::string::npos || pos < best)) best = pos;
    }
    size_t start = 0;
    if (best != std::string::npos) {
        start = best > 80 ? best - 80 : 0;
    }
    if (start > t.size()) start = 0;
    std::string slice = t.substr(start, static_cast<size_t>(std::max(80, max_chars)));
    slice = trim(slice);
    if (start > 0) slice = "…" + slice;
    if (start + slice.size() < t.size()) slice += "…";
    return slice;
}

double score_meta(const Document& d, const std::vector<std::string>& qtoks) {
    std::string blob = ascii_lower(d.title + " " + d.topic + " " + d.url + " " + d.source);
    double hit = 0;
    for (const auto& tok : qtoks) {
        if (blob.find(tok) != std::string::npos) hit += 4.0;
    }
    if (d.p_ai) hit += std::abs(*d.p_ai - 0.5) * 2.0;
    if (d.band == BAND_AI) hit += 0.5;
    return hit;
}

json measured_block(Store& store, int64_t dataset_id, const std::string& topic, const std::vector<int64_t>& ids) {
    json summary = summary_json(store, dataset_id, "documents", topic, ids);
    json measured;
    measured["shareOfDocuments"] = summary.contains("shareOfDocuments") ? summary["shareOfDocuments"] : json(nullptr);
    measured["shareOfAnalyzedWords"] =
        summary.contains("shareOfAnalyzedWords") ? summary["shareOfAnalyzedWords"] : json(nullptr);
    measured["shareOfDocumentsRange"] =
        summary.contains("shareOfDocumentsRange") ? summary["shareOfDocumentsRange"] : json(nullptr);
    measured["bands"] = summary.value("bands", json::object());
    measured["documentCount"] = summary.value("documentCount", 0);
    measured["headline"] = summary.value("headline", "");
    measured["analysisState"] = summary.value("analysisState", "");
    return measured;
}

json evidence_from(Store& store, int64_t dataset_id, const std::string& topic, const std::vector<int64_t>& ids,
                   const std::string& question, const Config& cfg) {
    auto metas = docs_for_view(store, dataset_id, topic, ids);
    auto qtoks = tokens_of(question);
    std::vector<std::pair<double, int64_t>> ranked;
    ranked.reserve(metas.size());
    for (const auto& d : metas) {
        ranked.push_back({score_meta(d, qtoks), d.id});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    int take = std::min(cfg.claude_max_excerpts * 3, static_cast<int>(ranked.size()));
    json evidence = json::array();
    std::unordered_set<int64_t> seen;
    for (int i = 0; i < take && static_cast<int>(evidence.size()) < cfg.claude_max_excerpts; ++i) {
        int64_t id = ranked[static_cast<size_t>(i)].second;
        if (!seen.insert(id).second) continue;
        auto full = store.get_document(id, true);
        if (!full) continue;
        json row;
        row["documentId"] = full->id;
        row["title"] = full->title;
        if (full->url.empty()) row["url"] = nullptr;
        else row["url"] = full->url;
        row["source"] = full->source;
        row["collectedAt"] = full->created_at.empty() ? json(nullptr) : json(full->created_at);
        row["publishedAt"] = full->published_at.empty() ? json(nullptr) : json(full->published_at);
        if (full->p_ai) row["pAi"] = *full->p_ai;
        else row["pAi"] = nullptr;
        row["band"] = full->band;
        row["excerpt"] = excerpt_of(full->text, cfg.claude_excerpt_chars, qtoks);
        evidence.push_back(std::move(row));
    }
    return evidence;
}

json empty_response(const json& measured, const json& evidence, const std::string& note) {
    json out;
    out["measured"] = measured;
    out["answer"] = note;
    out["inferred"] = false;
    out["source"] = "unavailable";
    out["claudeAvailable"] = false;
    out["evidence"] = evidence;
    return out;
}

std::string claude_text(const json& body) {
    if (!body.contains("content") || !body["content"].is_array()) return "";
    std::ostringstream os;
    for (const auto& block : body["content"]) {
        if (block.is_object() && block.value("type", "") == "text") {
            os << block.value("text", "");
        }
    }
    return trim(os.str());
}

std::string call_claude(const Config& cfg, const std::string& system, const std::string& user) {
    if (cfg.anthropic_api_key.empty()) return "";
    json req;
    req["model"] = cfg.anthropic_model;
    req["max_tokens"] = 1024;
    req["temperature"] = 0.2;
    req["system"] = system;
    req["messages"] = json::array({json{{"role", "user"}, {"content", user}}});
    std::vector<std::pair<std::string, std::string>> headers = {
        {"x-api-key", cfg.anthropic_api_key},
        {"anthropic-version", "2023-06-01"},
    };
    auto res = http_post_json("https://api.anthropic.com/v1/messages", UA, headers, req.dump(),
                              cfg.claude_timeout_ms, 512 * 1024);
    if (res.status < 200 || res.status >= 300) return "";
    try {
        return claude_text(json::parse(res.body));
    } catch (...) {
        return "";
    }
}

std::string system_prompt() {
    return "You explain KnowledgeOS results. KnowledgeOS already computed the measured numbers; never change them. "
           "Use only the provided excerpts and findings. Cite documents by documentId. "
           "Label what KnowledgeOS measured versus what you infer from excerpts. "
           "Do not invent documents, percentages, or medical advice. Do not mention HITS. "
           "GraphSAGE is not the headline AI-generated share.";
}

json run_query(Store& store, const Config& cfg, int64_t dataset_id, const std::string& question,
               const std::string& topic, const std::vector<int64_t>& ids, bool explain_mode) {
    json measured = measured_block(store, dataset_id, topic, ids);
    json evidence = evidence_from(store, dataset_id, topic, ids, question, cfg);
    json out;
    out["measured"] = measured;
    out["evidence"] = evidence;
    out["claudeAvailable"] = !cfg.anthropic_api_key.empty();

    std::string q = trim(question);
    if (q.empty() && !explain_mode) {
        out["answer"] = "Ask a question about this collection. KnowledgeOS numbers are in measured; Claude is unused.";
        out["inferred"] = false;
        out["source"] = "none";
        return out;
    }
    if (cfg.anthropic_api_key.empty()) {
        std::ostringstream note;
        note << "Claude is not configured (set ANTHROPIC_API_KEY). ";
        note << "KnowledgeOS measured share of documents: ";
        if (measured["shareOfDocuments"].is_null()) note << "not yet available";
        else note << measured["shareOfDocuments"].dump() << "%";
        note << ". Dashboard metrics and deterministic notes still apply.";
        out["answer"] = note.str();
        out["inferred"] = false;
        out["source"] = "unavailable";
        return out;
    }

    json user;
    user["task"] = explain_mode ? "explain" : "ask";
    user["question"] = explain_mode
                           ? "Write a short readable summary of this collection using only measured findings and excerpts."
                           : q;
    user["measuredByKnowledgeOS"] = measured;
    user["evidence"] = evidence;
    std::string answer = call_claude(cfg, system_prompt(), user.dump(2));
    if (answer.empty()) {
        return empty_response(measured, evidence,
                              "Claude did not respond. KnowledgeOS measured results are unchanged; try the dashboard notes.");
    }
    out["answer"] = answer;
    out["inferred"] = true;
    out["source"] = "claude";
    return out;
}

}  // namespace

nlohmann::json claude_status(const Config& cfg) {
    return json{
        {"configured", !cfg.anthropic_api_key.empty()},
        {"model", cfg.anthropic_model},
    };
}

nlohmann::json ask_dataset(Store& store, const Config& cfg, int64_t dataset_id, const std::string& question,
                           const std::string& topic, const std::vector<int64_t>& ids) {
    return run_query(store, cfg, dataset_id, question, topic, ids, false);
}

nlohmann::json explain_dataset(Store& store, const Config& cfg, int64_t dataset_id, const std::string& topic,
                               const std::vector<int64_t>& ids) {
    return run_query(store, cfg, dataset_id, "summarize stored findings", topic, ids, true);
}

}  // namespace kos
