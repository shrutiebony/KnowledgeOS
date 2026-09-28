#include "analysis.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>

namespace kos {

static std::vector<float> l2(std::vector<float> vector) {
    double n = 0;
    for (float v : vector) n += static_cast<double>(v) * v;
    if (n == 0) return vector;
    float inv = static_cast<float>(1.0 / std::sqrt(n));
    for (float& v : vector) v *= inv;
    return vector;
}

std::vector<float> hashed_embed(const std::string& text, int dim) {
    std::vector<float> vector(static_cast<size_t>(std::max(1, dim)), 0.f);
    if (is_blank(text)) return vector;
    std::string lower = ascii_lower(text);
    std::string token;
    auto add_token = [&](const std::string& t) {
        if (t.size() < 2) return;
        int32_t h = java_abs(java_hash_code(t));
        vector[static_cast<size_t>(java_abs(h) % dim)] += 1.0f;
        int32_t h2 = java_abs(java_hash_code(t + "#2"));
        vector[static_cast<size_t>(java_abs(h2) % dim)] += 0.5f;
    };
    for (unsigned char c : lower) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            token.push_back(static_cast<char>(c));
        } else {
            if (!token.empty()) add_token(token);
            token.clear();
        }
    }
    if (!token.empty()) add_token(token);
    for (size_t i = 0; i < text.size(); i += 7) {
        int8_t b = static_cast<int8_t>(text[i]);
        vector[static_cast<size_t>(floor_mod(static_cast<int>(b), dim))] += 0.05f;
    }
    return l2(vector);
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty()) return 0;
    size_t n = std::min(a.size(), b.size());
    double dot = 0, na = 0, nb = 0;
    for (size_t i = 0; i < n; ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na += static_cast<double>(a[i]) * a[i];
        nb += static_cast<double>(b[i]) * b[i];
    }
    if (na == 0 || nb == 0) return 0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

std::vector<float> centroid(const std::vector<std::vector<float>>& vectors) {
    if (vectors.empty() || vectors[0].empty()) return {};
    size_t n = vectors[0].size();
    std::vector<float> c(n, 0.f);
    int count = 0;
    for (const auto& v : vectors) {
        if (v.size() != n) continue;
        count++;
        for (size_t i = 0; i < n; ++i) c[i] += v[i];
    }
    if (count == 0) return c;
    for (size_t i = 0; i < n; ++i) c[i] /= static_cast<float>(count);
    return l2(c);
}

}  // namespace kos
