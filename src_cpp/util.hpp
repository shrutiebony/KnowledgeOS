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

inline std::string url_decode(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]);
            int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i] == '+' ? ' ' : s[i]);
    }
    return out;
}

inline std::string url_encode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '|') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

inline std::optional<std::string> normalize_url(std::string raw) {
    raw = trim(raw);
    if (raw.empty()) return std::nullopt;
    if (raw.rfind("//", 0) == 0) raw = "https:" + raw;
    else if (raw.find("://") == std::string::npos) raw = "https://" + raw;

    auto scheme_end = raw.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;
    std::string scheme = ascii_lower(raw.substr(0, scheme_end));
    if (scheme != "http" && scheme != "https") return std::nullopt;
    std::string rest = raw.substr(scheme_end + 3);
    if (rest.empty()) return std::nullopt;

    std::string hostport, pathquery;
    if (rest[0] == '[') {
        auto rb = rest.find(']');
        if (rb == std::string::npos) return std::nullopt;
        auto slash = rest.find('/', rb);
        hostport = rest.substr(0, slash == std::string::npos ? rest.size() : slash);
        pathquery = slash == std::string::npos ? "/" : rest.substr(slash);
    } else {
        auto slash = rest.find('/');
        auto qpos = rest.find('?');
        size_t cut = rest.size();
        if (slash != std::string::npos) cut = slash;
        else if (qpos != std::string::npos) cut = qpos;
        hostport = rest.substr(0, cut);
        pathquery = cut < rest.size() ? rest.substr(cut) : "/";
        if (pathquery.empty() || pathquery[0] == '?') pathquery = "/" + pathquery;
    }
    if (hostport.empty()) return std::nullopt;

    std::string host = hostport;
    int port = -1;
    auto colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
        host = hostport.substr(0, colon);
        try {
            port = std::stoi(hostport.substr(colon + 1));
        } catch (...) {
            return std::nullopt;
        }
    }
    host = ascii_lower(host);
    if (!host.empty() && host.back() == '.') host.pop_back();
    if (host.empty()) return std::nullopt;
    if ((port == 80 && scheme == "http") || (port == 443 && scheme == "https")) port = -1;

    std::string path, query;
    auto q = pathquery.find('?');
    if (q == std::string::npos) {
        path = pathquery;
    } else {
        path = pathquery.substr(0, q);
        query = pathquery.substr(q + 1);
    }
    if (path.empty()) path = "/";
    while (path.find("//") != std::string::npos) {
        auto pos = path.find("//");
        path.replace(pos, 2, "/");
    }
    if (path.size() > 1 && path.back() == '/') path.pop_back();

    std::string out = scheme + "://" + host;
    if (port != -1) out += ":" + std::to_string(port);
    out += path;
    if (!query.empty()) out += "?" + query;
    return out;
}

inline std::optional<std::string> host_of(const std::string& url) {
    auto n = normalize_url(url);
    if (!n) return std::nullopt;
    auto scheme = n->find("://");
    auto rest = n->substr(scheme + 3);
    auto slash = rest.find('/');
    auto hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    auto colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
        return hostport.substr(0, colon);
    }
    return hostport;
}

inline std::optional<std::string> comparable_host(const std::string& url) {
    auto h = host_of(url);
    if (!h) return std::nullopt;
    if (h->rfind("www.", 0) == 0) return h->substr(4);
    return h;
}

inline bool same_host(const std::string& a, const std::string& b) {
    auto ha = comparable_host(a);
    auto hb = comparable_host(b);
    return ha && hb && *ha == *hb;
}

inline std::optional<std::string> dedup_key(const std::string& url) {
    auto n = normalize_url(url);
    if (!n) return std::nullopt;
    std::string key = *n;
    if (key.rfind("https://", 0) == 0) key = key.substr(8);
    else if (key.rfind("http://", 0) == 0) key = key.substr(7);
    if (key.rfind("www.", 0) == 0) key = key.substr(4);
    return key;
}

inline std::optional<std::string> url_path(const std::string& url) {
    auto n = normalize_url(url);
    if (!n) return std::nullopt;
    auto scheme = n->find("://");
    auto rest = n->substr(scheme + 3);
    auto slash = rest.find('/');
    if (slash == std::string::npos) return "/";
    auto pathq = rest.substr(slash);
    auto q = pathq.find('?');
    return q == std::string::npos ? pathq : pathq.substr(0, q);
}

inline bool skippable_url(const std::string& url) {
    auto path = url_path(url);
    if (!path) return true;
    std::string lower = ascii_lower(*path);
    static const char* exts[] = {
        ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".zip", ".rar", ".7z",
        ".tar", ".gz", ".tgz", ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico",
        ".bmp", ".mp3", ".mp4", ".m4a", ".wav", ".webm", ".avi", ".mov", ".wmv", ".css",
        ".js", ".mjs", ".map", ".json", ".woff", ".woff2", ".ttf", ".eot", ".otf", ".exe",
        ".dmg", ".apk", ".iso", ".csv", ".tsv", ".zip", ".gz"
    };
    for (const char* e : exts) {
        if (lower.find(e) != std::string::npos) {
            auto pos = lower.rfind(e);
            if (pos != std::string::npos) {
                size_t after = pos + std::char_traits<char>::length(e);
                if (after == lower.size() || lower[after] == '?' || lower[after] == '.' || after == lower.size()) {
                    return true;
                }
                if (after < lower.size() && (lower[after] == '?' || after == lower.size())) return true;
                if (pos + std::char_traits<char>::length(e) == lower.size()) return true;
                char next = after < lower.size() ? lower[after] : '\0';
                if (next == 0 || next == '?' || !std::isalnum(static_cast<unsigned char>(next))) return true;
            }
        }
    }
    return false;
}

inline bool is_private_or_local_host(std::string host) {
    host = ascii_lower(trim(host));
    if (host.empty()) return true;
    if (host.rfind("www.", 0) == 0) host = host.substr(4);
    if (!host.empty() && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    if (host == "localhost" || host == "0.0.0.0" || host == "::1" || host == "127.0.0.1") return true;
    if (host.size() >= 6 && host.compare(host.size() - 6, 6, ".local") == 0) return true;
    int a = 0, b = 0, c = 0, d = 0;
    char extra = 0;
    if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) == 4) {
        if (a == 10) return true;
        if (a == 127) return true;
        if (a == 0) return true;
        if (a == 169 && b == 254) return true;
        if (a == 192 && b == 168) return true;
        if (a == 172 && b >= 16 && b <= 31) return true;
    }
    return false;
}

inline bool is_public_http_url(const std::string& url) {
    auto n = normalize_url(url);
    if (!n) return false;
    auto h = host_of(*n);
    return h && !is_private_or_local_host(*h);
}

inline std::optional<std::string> resolve_url(const std::string& base, std::string href) {
    href = trim(href);
    if (href.empty() || href[0] == '#' || starts_with_icase(href, "mailto:") ||
        starts_with_icase(href, "javascript:") || starts_with_icase(href, "data:") ||
        starts_with_icase(href, "tel:")) {
        return std::nullopt;
    }
    if (href.rfind("//", 0) == 0) return normalize_url("https:" + href);
    if (href.find("://") != std::string::npos) return normalize_url(href);

    auto nb = normalize_url(base);
    if (!nb) return std::nullopt;
    auto scheme = nb->find("://");
    auto rest = nb->substr(scheme + 3);
    auto slash = rest.find('/');
    std::string origin = nb->substr(0, scheme + 3) + (slash == std::string::npos ? rest : rest.substr(0, slash));
    std::string base_path = slash == std::string::npos ? "/" : rest.substr(slash);
    auto q = base_path.find('?');
    if (q != std::string::npos) base_path = base_path.substr(0, q);
    if (href[0] == '/') return normalize_url(origin + href);

    std::string dir = base_path;
    auto last = dir.rfind('/');
    dir = last == std::string::npos ? "/" : dir.substr(0, last + 1);
    return normalize_url(origin + dir + href);
}

inline std::string html_unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        auto semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (ent == "nbsp") out.push_back(' ');
        else if (!ent.empty() && ent[0] == '#') {
            int code = 0;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                try { code = std::stoi(ent.substr(2), nullptr, 16); } catch (...) { code = 0; }
            } else {
                try { code = std::stoi(ent.substr(1)); } catch (...) { code = 0; }
            }
            if (code > 0 && code < 128) out.push_back(static_cast<char>(code));
            else if (code >= 128 && code <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code > 0x7FF && code <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(' ');
            }
        } else {
            out.push_back('&');
            continue;
        }
        i = semi;
    }
    return out;
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
