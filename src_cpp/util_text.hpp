#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kos {

inline double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

inline std::string ascii_lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

inline std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

inline bool starts_with_icase(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) return false;
    return iequals(value.substr(0, prefix.size()), prefix);
}

inline std::string empty_to_null_str(const std::string& s) {
    return trim(s);
}

inline bool is_blank(const std::string& s) {
    return trim(s).empty();
}

inline std::optional<std::string> nonempty(const std::string& s) {
    auto t = trim(s);
    if (t.empty()) return std::nullopt;
    return t;
}

inline int32_t java_hash_code(std::string_view s) {
    int32_t h = 0;
    for (unsigned char c : s) {
        h = 31 * h + static_cast<int32_t>(c);
    }
    return h;
}

inline int32_t java_abs(int32_t v) {
    return v == INT32_MIN ? v : (v < 0 ? -v : v);
}

inline int floor_mod(int x, int y) {
    int r = x % y;
    if ((r ^ y) < 0 && r != 0) r += y;
    return r;
}

inline double round3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

inline int pct(double p) {
    return static_cast<int>(std::round(clamp01(p) * 100.0));
}

inline std::string clip(const std::string& s, size_t max) {
    return s.size() <= max ? s : s.substr(0, max);
}

inline std::string now_iso() {
    using namespace std::chrono;
    auto t = time_point_cast<seconds>(system_clock::now());
    auto tt = system_clock::to_time_t(t);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline int iso_year(const std::string& date) {
    if (date.size() < 4) return 0;
    try {
        int y = std::stoi(date.substr(0, 4));
        return (y >= 1900 && y <= 2100) ? y : 0;
    } catch (...) {
        return 0;
    }
}

inline int utc_year_now() {
    return iso_year(now_iso());
}

inline std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            auto t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    auto t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

inline std::string guess_topic(const std::string& text) {
    std::string lower = ascii_lower(text);
    if (lower.find("medicine") != std::string::npos || lower.find("clinical") != std::string::npos ||
        lower.find("patient") != std::string::npos || lower.find("disease") != std::string::npos) {
        return "medicine";
    }
    if (lower.find("algorithm") != std::string::npos || lower.find("computer") != std::string::npos ||
        lower.find("software") != std::string::npos || lower.find("graph") != std::string::npos) {
        return "computer_science";
    }
    if (lower.find("history") != std::string::npos || lower.find("empire") != std::string::npos ||
        lower.find("war") != std::string::npos) {
        return "history";
    }
    if (lower.find("physics") != std::string::npos || lower.find("quantum") != std::string::npos ||
        lower.find("particle") != std::string::npos) {
        return "physics";
    }
    if (lower.find("biology") != std::string::npos || lower.find("genome") != std::string::npos ||
        lower.find("species") != std::string::npos) {
        return "biology";
    }
    return "general";
}

}  // namespace kos
