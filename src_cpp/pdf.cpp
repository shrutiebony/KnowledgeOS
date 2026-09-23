#include "pdf.hpp"
#include "util.hpp"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace kos {

static std::string pdf_unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else if (n >= '0' && n <= '7') {
                int v = n - '0';
                int k = 0;
                while (k < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7') {
                    v = v * 8 + (s[++i] - '0');
                    ++k;
                }
                out.push_back(static_cast<char>(v));
            } else {
                out.push_back(n);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static void extract_strings(const std::string& data, std::string& acc) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '(') {
            std::string cur;
            int depth = 1;
            ++i;
            while (i < data.size() && depth > 0) {
                if (data[i] == '\\' && i + 1 < data.size()) {
                    cur.push_back('\\');
                    cur.push_back(data[++i]);
                } else if (data[i] == '(') {
                    depth++;
                    cur.push_back('(');
                } else if (data[i] == ')') {
                    depth--;
                    if (depth > 0) cur.push_back(')');
                } else {
                    cur.push_back(data[i]);
                }
                ++i;
            }
            --i;
            std::string t = pdf_unescape(cur);
            if (!t.empty()) {
                if (!acc.empty() && acc.back() != ' ' && acc.back() != '\n') acc.push_back(' ');
                acc += t;
            }
        }
    }
}

static std::string inflate_raw(const unsigned char* data, size_t n) {
    mz_ulong out_len = static_cast<mz_ulong>(std::max<size_t>(n * 8, 4096));
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string out(out_len, '\0');
        mz_ulong dest = out_len;
        int st = mz_uncompress(reinterpret_cast<unsigned char*>(out.data()), &dest,
                               data, static_cast<mz_ulong>(n));
        if (st == MZ_OK) {
            out.resize(dest);
            return out;
        }
        if (st == MZ_BUF_ERROR) {
            out_len *= 2;
            continue;
        }
        // try raw deflate (no zlib header)
        tinfl_decompressor inf;
        tinfl_init(&inf);
        size_t in_sz = n;
        size_t out_sz = out_len;
        std::string raw(out_sz, '\0');
        tinfl_status ts = tinfl_decompress(&inf, data, &in_sz, reinterpret_cast<unsigned char*>(raw.data()),
                                           reinterpret_cast<unsigned char*>(raw.data()), &out_sz,
                                           TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (ts == TINFL_STATUS_DONE) {
            raw.resize(out_sz);
            return raw;
        }
        break;
    }
    return "";
}

std::string extract_pdf_text(const std::string& bytes) {
    if (bytes.size() < 5 || bytes.compare(0, 4, "%PDF") != 0) return "";
    std::string acc;
    extract_strings(bytes, acc);
    size_t pos = 0;
    while (true) {
        auto s = bytes.find("stream", pos);
        if (s == std::string::npos) break;
        size_t start = s + 6;
        if (start < bytes.size() && bytes[start] == '\r') ++start;
        if (start < bytes.size() && bytes[start] == '\n') ++start;
        auto e = bytes.find("endstream", start);
        if (e == std::string::npos) break;
        std::string dict;
        size_t dict_from = s > 400 ? s - 400 : 0;
        dict = bytes.substr(dict_from, s - dict_from);
        bool flate = dict.find("/FlateDecode") != std::string::npos || dict.find("/Fl") != std::string::npos;
        std::string payload = bytes.substr(start, e - start);
        if (!payload.empty() && payload.back() == '\n') payload.pop_back();
        if (!payload.empty() && payload.back() == '\r') payload.pop_back();
        std::string decoded = payload;
        if (flate) {
            decoded = inflate_raw(reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
        }
        extract_strings(decoded, acc);
        pos = e + 9;
    }
    // collapse whitespace
    std::string out;
    bool space = true;
    for (unsigned char c : acc) {
        if (c < 32 && c != '\n' && c != '\t') continue;
        if (std::isspace(c)) {
            if (!space) {
                out.push_back(' ');
                space = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            space = false;
        }
    }
    return trim(out);
}

}  // namespace kos
