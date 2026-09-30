#pragma once

#include "analysis.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace kos {

int doc_year(const Document& d);
bool is_pre_2019(const Document& d);
bool is_post_2019(const Document& d);
void apply_era_scores(std::vector<Document>& docs, const Config& cfg, bool allow_keep_stored);

inline double nz(const std::optional<double>& v) { return v ? *v : 0; }

inline double parse_signal(const std::string& json_s, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto i = json_s.find(needle);
    if (i == std::string::npos) return 0;
    size_t start = i + needle.size();
    size_t end = start;
    while (end < json_s.size() && std::string("0123456789.+-eE").find(json_s[end]) != std::string::npos) end++;
    try {
        return std::stod(json_s.substr(start, end - start));
    } catch (...) {
        return 0;
    }
}

inline int year_of_date(const std::string& date) {
    auto is_year = [](int y) { return y >= 1900 && y <= 2100; };
    if (date.size() >= 4) {
        bool digits = true;
        for (int i = 0; i < 4; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(date[static_cast<size_t>(i)]))) {
                digits = false;
                break;
            }
        }
        if (digits) {
            try {
                int y = std::stoi(date.substr(0, 4));
                if (is_year(y)) return y;
            } catch (...) {
            }
        }
    }
    for (size_t i = 0; i + 3 < date.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(date[i])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 1])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 2])) ||
            !std::isdigit(static_cast<unsigned char>(date[i + 3]))) {
            continue;
        }
        const bool left_ok = i == 0 || !std::isdigit(static_cast<unsigned char>(date[i - 1]));
        const bool right_ok = i + 4 >= date.size() || !std::isdigit(static_cast<unsigned char>(date[i + 4]));
        if (!left_ok || !right_ok) continue;
        try {
            int y = std::stoi(date.substr(i, 4));
            if (is_year(y)) return y;
        } catch (...) {
        }
    }
    return 0;
}

// Best available document date: first-revision / createdAt, else publishedAt.
inline std::string doc_date_of(const Document& d) {
    if (!d.created_at.empty() && year_of_date(d.created_at) > 0) return d.created_at;
    if (!d.published_at.empty() && year_of_date(d.published_at) > 0) return d.published_at;
    return "";
}

inline std::string edge_reason(bool topic, bool source) {
    std::string r = "embedding";
    if (topic) r += "+topic";
    if (source) r += "+source";
    return r;
}

struct ScoredNeighbor {
    size_t j;
    double score;
    double cos;
    bool topic;
    bool source;
};

inline std::vector<ScoredNeighbor> knn_neighbors(const Document& a, const std::vector<Document>& docs,
                                                int k, double min_cos) {
    std::vector<ScoredNeighbor> neighbors;
    for (size_t j = 0; j < docs.size(); ++j) {
        const auto& b = docs[j];
        if (a.id == b.id) continue;
        double cos = cosine(a.embedding, b.embedding);
        bool topic_match = !a.topic.empty() && a.topic == b.topic;
        bool source_match = !a.source.empty() && a.source == b.source;
        double score = cos + (topic_match ? 0.08 : 0) + (source_match ? 0.04 : 0);
        if (cos >= min_cos || topic_match) {
            neighbors.push_back({j, score, cos, topic_match, source_match});
        }
    }
    std::sort(neighbors.begin(), neighbors.end(),
              [](const ScoredNeighbor& x, const ScoredNeighbor& y) { return x.score > y.score; });
    if (k >= 0 && static_cast<int>(neighbors.size()) > k) neighbors.resize(k);
    return neighbors;
}

inline std::vector<DocumentEdge> knn_edges(const std::vector<Document>& docs, int k, double min_cos,
                                          int64_t edge_dataset_id) {
    std::vector<DocumentEdge> created;
    for (const auto& a : docs) {
        for (const auto& s : knn_neighbors(a, docs, k, min_cos)) {
            DocumentEdge e;
            e.dataset_id = edge_dataset_id;
            e.source_document_id = a.id;
            e.target_document_id = docs[s.j].id;
            e.cosine = s.cos;
            e.reason = edge_reason(s.topic, s.source);
            created.push_back(e);
        }
    }
    return created;
}

}  // namespace kos
