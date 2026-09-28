#include "analysis.hpp"
#include "util.hpp"

#include <cmath>

namespace kos {

static int year_of(const std::string& date) {
    if (date.size() < 4) return 0;
    try {
        return std::stoi(date.substr(0, 4));
    } catch (...) {
        return 0;
    }
}

static int month_of(const std::string& date) {
    if (date.size() < 7) return 1;
    try {
        return std::stoi(date.substr(5, 2));
    } catch (...) {
        return 1;
    }
}

static int day_of(const std::string& date) {
    if (date.size() < 10) return 1;
    try {
        return std::stoi(date.substr(8, 2));
    } catch (...) {
        return 1;
    }
}

double post_chatgpt_signal(const std::string& published_at) {
    if (published_at.empty()) return 0.45;
    int year = year_of(published_at);
    int month = month_of(published_at);
    int day = day_of(published_at);
    if (year <= 0) return 0.45;
    if (year < 2019) return 0.0;
    if (year < 2022) return 0.20;
    bool before_launch = year < 2022 || (year == 2022 && (month < 11 || (month == 11 && day < 30)));
    if (before_launch) return 0.26;
    if (year == 2022) return 0.48;
    double base = 0.54 + 0.05 * std::min(4, year - 2023);
    double month_part = month / 12.0 * 0.04;
    return clamp01(base + month_part);
}

std::string band_of(double p_ai, double interval, const Config& cfg) {
    if (interval > cfg.band_max_interval) return BAND_UNC;
    if (p_ai >= cfg.band_ai_min) return BAND_AI;
    if (p_ai <= cfg.band_human_max) return BAND_HUMAN;
    return BAND_UNC;
}

static double sigmoid(double z) {
    return 1.0 / (1.0 + std::exp(-z));
}

Estimate calibrate(const std::map<std::string, double>& raw_signals, const Config& cfg) {
    std::map<std::string, double> signals;
    for (const auto& [k, v] : raw_signals) {
        if (!std::isnan(v)) signals[k] = clamp01(v);
    }
    double stylometry = signals.count("stylometry") ? signals["stylometry"] : 0.5;
    double detector_a = signals.count("stock_phrase_detector") ? signals["stock_phrase_detector"] : 0.5;
    double detector_b = signals.count("sentence_uniformity_detector") ? signals["sentence_uniformity_detector"] : 0.5;
    double detector_c = signals.count("ngram_repetition_detector") ? signals["ngram_repetition_detector"] : 0.5;
    double anomaly = signals.count("embedding_anomaly") ? signals["embedding_anomaly"] : 0.5;
    double deviation = signals.count("stylometry_deviation") ? signals["stylometry_deviation"] : 0.5;
    double recency = signals.count("post_chatgpt") ? signals["post_chatgpt"] : 0.45;

    double z = 0.58
        + 0.70 * (stylometry - 0.5)
        + 1.10 * (detector_a - 0.5)
        + 0.85 * (detector_b - 0.5)
        + 0.28 * (detector_c - 0.5)
        + 0.50 * (anomaly - 0.5)
        + 0.30 * (deviation - 0.5)
        + 1.15 * (recency - 0.45);
    double p = sigmoid(z);

    double core[3] = {stylometry, detector_b, recency};
    double mean = (core[0] + core[1] + core[2]) / 3.0;
    double var = 0;
    for (double v : core) var += (v - mean) * (v - mean);
    double disagreement = std::sqrt(var / 3.0);
    double half = std::min(0.22, 0.06 + 0.45 * disagreement);
    Estimate e;
    e.p_ai = p;
    e.ci_low = clamp01(p - half);
    e.ci_high = clamp01(p + half);
    e.band = band_of(p, e.ci_high - e.ci_low, cfg);
    e.signals = signals;
    return e;
}

Estimate mix_with_rank(const Estimate& raw, double rank01, const Config& cfg) {
    double mix = clamp01(cfg.rank_mix);
    double mixed = clamp01((1.0 - mix) * raw.p_ai + mix * clamp01(rank01));
    double half = std::max(0.05, (raw.ci_high - raw.ci_low) / 2.0);
    Estimate e = raw;
    e.p_ai = mixed;
    e.ci_low = clamp01(mixed - half);
    e.ci_high = clamp01(mixed + half);
    e.band = band_of(mixed, e.ci_high - e.ci_low, cfg);
    return e;
}

}  // namespace kos
