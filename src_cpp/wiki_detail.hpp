#pragma once

#include "util.hpp"

#include <optional>
#include <string>

namespace kos {

inline constexpr const char* WIKI_HTTP_UA = "KnowledgeOS-Wiki/1.0 (local educational corpus ingest; en.wikipedia.org general collection)";

inline std::optional<std::string> parse_iso_date(const std::string& raw) {
    std::string value = trim(raw);
    if (value.size() >= 10 && value[4] == '-' && value[7] == '-') return value.substr(0, 10);
    return std::nullopt;
}

}  // namespace kos
