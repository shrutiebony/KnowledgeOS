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


#include "claude_internal.hpp"

namespace kos {
namespace {

constexpr const char* UA = "KnowledgeOS/1.0 (claude-adapter)";

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

}  // namespace

namespace claude_detail {

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

}  // namespace claude_detail

nlohmann::json claude_status(const Config& cfg) {
    return json{
        {"configured", !cfg.gemini_api_key.empty()},
        {"model", resolve_gemini_model(cfg.gemini_model)},
    };
}

}  // namespace kos
