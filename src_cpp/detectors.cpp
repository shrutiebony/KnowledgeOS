#include "analysis.hpp"
#include "util.hpp"

namespace kos {

std::vector<DetectorScore> run_detectors(const std::string& text, const Features& f) {
    static const char* markers[] = {
        "it is important to note",
        "in conclusion",
        "this article provides",
        "plays a crucial role",
        "in today's world",
        "a comprehensive overview",
        "it should be noted",
        "various factors",
        "in this article we will",
        "delve into"
    };
    std::string lower = ascii_lower(text);
    int hits = 0;
    for (const char* m : markers) {
        if (lower.find(m) != std::string::npos) hits++;
    }
    double stock = clamp01(hits / 4.0);
    double burst = f.burstiness;
    double stdv = f.sentence_length_std;
    double uniform = clamp01(0.6 * (1.0 - burst / 1.1) + 0.4 * (1.0 - stdv / 16.0));
    double rep = clamp01(f.repetition_score * 1.4);
    return {
        {"stock_phrase_detector", stock},
        {"sentence_uniformity_detector", uniform},
        {"ngram_repetition_detector", rep}
    };
}

}  // namespace kos
