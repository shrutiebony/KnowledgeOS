#pragma once

#include "config.hpp"

#include <string>

namespace kos {

struct GeminiCall {
    std::string text;
    std::string error;
};

namespace claude_detail {

GeminiCall call_gemini(const Config& cfg, const std::string& system, const std::string& user);

}  // namespace claude_detail
}  // namespace kos
