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
#include <iostream>
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

json empty_response(const json& measured, const json& evidence, const std::string& note, bool gemini_configured) {
    json out;
    out["measured"] = measured;
    out["answer"] = note;
    out["inferred"] = false;
    out["source"] = "unavailable";
    out["geminiAvailable"] = gemini_configured;
    out["evidence"] = evidence;
    return out;
}

struct GeminiCall {
    std::string text;
    std::string error;
};

std::string clip_note(std::string s, size_t max_chars) {
    s = trim(s);
    if (s.size() > max_chars) s.resize(max_chars);
    return s;
}

std::string resolve_gemini_model(const std::string& configured) {
    std::string model = trim(configured);
    if (model.empty()) model = "gemini-3.1-flash-lite";
    // gemini-2.0-flash and its pinned variants were shut down on 2026-06-01.
    if (model.rfind("gemini-2.0", 0) == 0) {
        std::cerr << "GEMINI_MODEL " << model << " is shut down; using gemini-3.1-flash-lite\n";
        return "gemini-3.1-flash-lite";
    }
    return model;
}

std::string gemini_visible_text(const json& body) {
    if (!body.contains("candidates") || !body["candidates"].is_array() || body["candidates"].empty()) return "";
    const json& content = body["candidates"][0].value("content", json::object());
    if (!content.contains("parts") || !content["parts"].is_array()) return "";
    std::ostringstream os;
    for (const auto& part : content["parts"]) {
        if (!part.is_object() || !part.contains("text") || !part["text"].is_string()) continue;
        if (part.contains("thought") && part["thought"].is_boolean() && part["thought"].get<bool>()) continue;
        os << part["text"].get<std::string>();
    }
    return trim(os.str());
}

std::string gemini_failure(const json& body, const HttpResponse& res) {
    std::string msg;
    if (body.contains("error") && body["error"].is_object()) {
        msg = body["error"].value("message", "");
    }
    if (msg.empty() && body.contains("candidates") && body["candidates"].is_array() && !body["candidates"].empty()) {
        msg = body["candidates"][0].value("finishReason", "");
        if (msg == "STOP" || msg == "MAX_TOKENS") msg.clear();
    }
    if (msg.empty() && body.contains("promptFeedback") && body["promptFeedback"].is_object()) {
        msg = body["promptFeedback"].value("blockReason", "");
    }
    if (msg.empty()) msg = res.error;
    if (msg.empty()) msg = "HTTP " + std::to_string(res.status);
    return "Gemini did not respond (" + clip_note(msg, 240) + "). The measured scores are unchanged.";
}

GeminiCall call_gemini(const Config& cfg, const std::string& system, const std::string& user) {
    if (cfg.gemini_api_key.empty()) return {"", "Set GEMINI_API_KEY to have Ask answer from these readings."};
    std::string model = resolve_gemini_model(cfg.gemini_model);
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent";
    json req;
    req["systemInstruction"] = json{{"parts", json::array({json{{"text", system}}})}};
    req["contents"] = json::array({
        json{{"role", "user"}, {"parts", json::array({json{{"text", user}}})}}
    });
    json gen = json{{"maxOutputTokens", 4096}};
    if (model.rfind("gemini-3", 0) == 0) {
        // Gemini 3.x degrades when temperature is overridden. Low thinking keeps the reply in the answer text.
        gen["thinkingConfig"] = json{{"thinkingLevel", "low"}};
    } else {
        gen["temperature"] = 0.3;
    }
    req["generationConfig"] = std::move(gen);
    std::vector<std::pair<std::string, std::string>> headers = {
        {"x-goog-api-key", cfg.gemini_api_key},
    };
    auto res = http_post_json(url, UA, headers, req.dump(), cfg.claude_timeout_ms, 512 * 1024);
    json body = json::object();
    try {
        if (!res.body.empty()) body = json::parse(res.body);
    } catch (...) {
        body = json::object();
    }
    if (!res.error.empty() || res.status < 200 || res.status >= 300) {
        std::string note = gemini_failure(body, res);
        std::cerr << note << "\n";
        return {"", note};
    }
    std::string text = gemini_visible_text(body);
    if (text.empty()) {
        std::string note = gemini_failure(body, res);
        if (note.find("HTTP 200") != std::string::npos) {
            note = "Gemini returned no answer text. The measured scores are unchanged.";
        }
        std::cerr << note << "\n";
        return {"", note};
    }
    return {text, ""};
}

std::string measured_brief(const json& measured) {
    std::ostringstream os;
    int n = measured.value("documentCount", 0);
    os << "Pages looked at: " << n << ".\n";
    if (!measured.contains("shareOfDocuments") || measured["shareOfDocuments"].is_null()) {
        os << "AI share: not available yet.\n";
    } else {
        os << "About " << measured["shareOfDocuments"].dump() << "% of the text looks AI-generated.\n";
    }
    if (measured.contains("bands") && measured["bands"].is_object()) {
        const json& bands = measured["bands"];
        os << "Labels: " << bands.value("LIKELY_AI", 0) << " likely AI-generated, "
           << bands.value("LIKELY_HUMAN", 0) << " likely human-written, "
           << bands.value("UNCERTAIN", 0) << " uncertain.\n";
    }
    return os.str();
}

std::string layman_answer(const std::string& question, const json& measured) {
    int n = measured.value("documentCount", 0);
    std::ostringstream os;
    os << "We looked at " << n << (n == 1 ? " page. " : " pages. ");
    if (!measured.contains("shareOfDocuments") || measured["shareOfDocuments"].is_null()) {
        os << "The writing checks do not have a result for this view yet.";
        return os.str();
    }
    os << "From the writing checks, about " << measured["shareOfDocuments"].dump()
       << "% of the text looks AI-generated. ";
    std::string q = ascii_lower(question);
    if (q.find("why") != std::string::npos || q.find("high") != std::string::npos) {
        os << "That percent is high when many of the pages score high on those checks. ";
    } else if (q.find("how") != std::string::npos) {
        os << "Each page is checked for writing that looks formulaic, and the percent is the average of those page scores. ";
    } else {
        os << "That percent is the average of the page scores. ";
    }
    os << "It is an estimate, not proof of who wrote the pages.";
    return os.str();
}

std::string system_prompt() {
    return "You answer questions for a layperson about a KnowledgeOS collection. "
           "KnowledgeOS estimates how much of the writing looks AI-generated. "
           "The measured numbers are already computed. Never change them and never invent a percent. "
           "Each page is checked for formulaic wording, stock phrases, even sentence lengths, and repeated phrases, "
           "then compared with earlier writing in the same collection. The percent is the average of those page scores. "
           "It is an estimate, not proof of who wrote a page. "
           "Answer the user's question directly in 2 to 6 short everyday sentences. "
           "Use the measured numbers. Do not cite document ids, titles, URLs, or excerpts. "
           "Do not list pages. Do not show JSON or formulas. "
           "If the numbers do not answer the question, say so in one plain sentence. "
           "No medical advice. Reply with the answer only.";
}

json run_query(Store& store, const Config& cfg, int64_t dataset_id, const std::string& question,
               const std::string& topic, const std::vector<int64_t>& ids, bool explain_mode) {
    json measured = measured_block(store, dataset_id, topic, ids);
    json evidence = evidence_from(store, dataset_id, topic, ids, question, cfg);
    json out;
    out["measured"] = measured;
    out["evidence"] = evidence;
    out["geminiAvailable"] = !cfg.gemini_api_key.empty();

    std::string q = trim(question);
    if (q.empty() && !explain_mode) {
        out["answer"] = "Ask a question about this collection. KnowledgeOS numbers are in the measured readings.";
        out["inferred"] = false;
        out["source"] = "none";
        return out;
    }
    if (cfg.gemini_api_key.empty()) {
        out["answer"] = layman_answer(q, measured);
        out["inferred"] = false;
        out["source"] = "unavailable";
        return out;
    }

    std::string asked = explain_mode
                            ? "Explain what this collection shows, in a few short everyday sentences."
                            : q;
    std::ostringstream prompt;
    prompt << "Question: " << asked
           << "\n\nWhat this collection already shows. Do not change these numbers:\n"
           << measured_brief(measured)
           << "\nAnswer the question in plain language. Do not cite pages.";
    GeminiCall reply = call_gemini(cfg, system_prompt(), prompt.str());
    if (reply.text.empty()) {
        return empty_response(measured, evidence, layman_answer(asked, measured), true);
    }
    out["answer"] = reply.text;
    out["inferred"] = true;
    out["source"] = "gemini";
    return out;
}

}  // namespace

nlohmann::json claude_status(const Config& cfg) {
    return json{
        {"configured", !cfg.gemini_api_key.empty()},
        {"model", resolve_gemini_model(cfg.gemini_model)},
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
