#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kos {

inline constexpr const char* KIND_WIKI = "WIKIPEDIA_SAMPLE";
inline constexpr const char* KIND_WIKI_SUBSET = "WIKIPEDIA_SUBSET";
inline constexpr const char* KIND_UPLOAD = "USER_UPLOAD";
inline constexpr const char* KIND_URLS = "USER_URLS";

inline constexpr const char* BAND_AI = "LIKELY_AI";
inline constexpr const char* BAND_HUMAN = "LIKELY_HUMAN";
inline constexpr const char* BAND_UNC = "UNCERTAIN";
inline constexpr const char* BAND_PENDING = "PENDING";

inline constexpr const char* GS_NOT_TRAINED = "NOT_TRAINED";
inline constexpr const char* GS_EMBEDDED = "EMBEDDED";
inline constexpr const char* GS_VALIDATION_FAILED = "VALIDATION_FAILED";
inline constexpr const char* GS_VALIDATED = "VALIDATED";

inline constexpr const char* GS_DEFAULT_MSG =
    "GraphSAGE representations may be computed as graph context. They are not a detection signal until a labeled classifier is trained and validated.";

struct Dataset {
    int64_t id = 0;
    std::string name;
    std::string kind;
    std::string parent_topic;
    std::optional<int64_t> parent_dataset_id;
    std::string graph_sage_status = GS_NOT_TRAINED;
    std::string graph_sage_message = GS_DEFAULT_MSG;
    std::string created_at;
    std::string last_analyzed_at;
    std::string analysis_state = "idle";
};

struct Document {
    int64_t id = 0;
    int64_t dataset_id = 0;
    std::string title;
    std::string url;
    std::string source;
    std::string topic;
    std::string published_at;
    std::string text;
    int word_count = 0;
    std::vector<float> embedding;
    std::vector<float> gnn_embedding;
    std::optional<double> type_token_ratio;
    std::optional<double> avg_sentence_length;
    std::optional<double> sentence_length_std;
    std::optional<double> burstiness;
    std::optional<double> punctuation_ratio;
    std::optional<double> char_entropy;
    std::optional<double> repetition_score;
    std::optional<double> detector_stylometry;
    std::optional<double> detector_repetition;
    std::optional<double> detector_uniformity;
    std::optional<double> embedding_anomaly;
    std::optional<double> stylometry_deviation;
    std::optional<double> p_ai;
    std::optional<double> ci_low;
    std::optional<double> ci_high;
    std::string band = BAND_PENDING;
    std::string explanation_json;
};

struct DocumentEdge {
    int64_t id = 0;
    int64_t dataset_id = 0;
    int64_t source_document_id = 0;
    int64_t target_document_id = 0;
    double cosine = 0;
    std::string reason;
};

}  // namespace kos
