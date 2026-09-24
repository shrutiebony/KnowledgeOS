#pragma once

#include "config.hpp"
#include "db.hpp"
#include "models.hpp"

#include "json.hpp"

#include <map>
#include <string>
#include <vector>

namespace kos {

struct Features {
    int word_count = 0;
    double type_token_ratio = 0;
    double avg_sentence_length = 0;
    double sentence_length_std = 0;
    double burstiness = 0;
    double punctuation_ratio = 0;
    double char_entropy = 0;
    double repetition_score = 0;
};

Features stylometry_analyze(const std::string& text);
double stylometry_ai_score(const Features& f);

struct DetectorScore {
    std::string name;
    double score = 0;
};

std::vector<DetectorScore> run_detectors(const std::string& text, const Features& f);

std::vector<float> hashed_embed(const std::string& text, int dim);
double cosine(const std::vector<float>& a, const std::vector<float>& b);
std::vector<float> centroid(const std::vector<std::vector<float>>& vectors);

struct Estimate {
    double p_ai = 0;
    double ci_low = 0;
    double ci_high = 0;
    std::string band = BAND_UNC;
    std::map<std::string, double> signals;
};

double post_chatgpt_signal(const std::string& published_at);
Estimate calibrate(const std::map<std::string, double>& raw, const Config& cfg);
Estimate mix_with_rank(const Estimate& raw, double rank01, const Config& cfg);
std::string band_of(double p_ai, double interval, const Config& cfg);

std::vector<double> ranks01(const std::vector<double>& values);

nlohmann::json analyze_dataset(Store& store, const Config& cfg, int64_t dataset_id, bool incremental);
void mean_aggregate_graphsage(Store& store, Dataset& dataset);
nlohmann::json train_graphsage(Store& store, int64_t dataset_id, const std::string& labels_path);

nlohmann::json era_traits_json(Store& store, int64_t dataset_id, const std::string& topic);
nlohmann::json summary_json(Store& store, int64_t dataset_id, const std::string& metric,
                            const std::string& topic, const std::vector<int64_t>& ids);
nlohmann::json graph_json(Store& store, int64_t dataset_id, const std::string& topic,
                          const std::vector<int64_t>& ids, int graph_k = 8,
                          double graph_min_cosine = 0.32);
nlohmann::json examples_json(Store& store, int64_t dataset_id, const std::string& band, int limit,
                             const std::string& topic, const std::vector<int64_t>& ids);
nlohmann::json ai_common_json(Store& store, int64_t dataset_id, const std::string& topic,
                              const std::vector<int64_t>& ids);
nlohmann::json breakdown_rows(Store& store, int64_t dataset_id, const std::string& by,
                              const std::string& topic, const std::vector<int64_t>& ids);
nlohmann::json explanation_json(Store& store, int64_t dataset_id, int64_t doc_id, int graph_k = 8,
                                double graph_min_cosine = 0.32);
nlohmann::json explain_insights(Store& store, int64_t dataset_id, const std::string& topic,
                                const std::vector<int64_t>& ids);
nlohmann::json era_cohorts_json(Store& store, int64_t dataset_id, const std::string& topic);
std::vector<std::string> topics_for_dataset(Store& store, int64_t dataset_id);

std::vector<Document> docs_for_view(Store& store, int64_t dataset_id, const std::string& topic,
                                    const std::vector<int64_t>& ids);

}  // namespace kos
