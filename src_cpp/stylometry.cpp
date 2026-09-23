#include "analysis.hpp"
#include "util.hpp"

#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace kos {

static std::vector<std::string> words_alnum(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : ascii_lower(text)) {
        if (std::isalnum(c) || c == '\'') {
            cur.push_back(static_cast<char>(c));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static double mean_i(const std::vector<int>& v) {
    if (v.empty()) return 0;
    double s = 0;
    for (int x : v) s += x;
    return s / static_cast<double>(v.size());
}

static double std_i(const std::vector<int>& v, double mean) {
    if (v.size() < 2) return 0;
    double var = 0;
    for (int x : v) {
        double d = x - mean;
        var += d * d;
    }
    return std::sqrt(var / static_cast<double>(v.size() - 1));
}

static double entropy_of(const std::string& text) {
    if (text.empty()) return 0;
    int counts[256] = {};
    int n = 0;
    for (unsigned char c : text) {
        counts[c]++;
        n++;
    }
    double h = 0;
    for (int count : counts) {
        if (count == 0) continue;
        double p = count / static_cast<double>(n);
        h -= p * (std::log(p) / std::log(2.0));
    }
    return h;
}

static double repetition_of(const std::vector<std::string>& words) {
    if (words.size() < 6) return 0;
    std::unordered_map<std::string, int> trigrams;
    int total = 0;
    for (size_t i = 0; i + 2 < words.size(); ++i) {
        std::string g = words[i] + " " + words[i + 1] + " " + words[i + 2];
        trigrams[g]++;
        total++;
    }
    int repeats = 0;
    for (auto& [_, c] : trigrams) {
        if (c > 1) repeats += c - 1;
    }
    return clamp01(repeats / static_cast<double>(std::max(1, total)));
}

Features stylometry_analyze(const std::string& text_in) {
    const std::string& text = text_in;
    auto words = words_alnum(text);
    int word_count = static_cast<int>(words.size());
    std::unordered_set<std::string> uniq(words.begin(), words.end());
    double ttr = word_count == 0 ? 0 : uniq.size() / static_cast<double>(word_count);

    std::vector<int> sentence_lens;
    std::string trimmed = trim(text);
    std::string cur;
    auto flush = [&]() {
        int n = static_cast<int>(words_alnum(cur).size());
        if (n > 0) sentence_lens.push_back(n);
        cur.clear();
    };
    for (size_t i = 0; i < trimmed.size(); ++i) {
        cur.push_back(trimmed[i]);
        char c = trimmed[i];
        if ((c == '.' || c == '!' || c == '?') && i + 1 < trimmed.size() &&
            std::isspace(static_cast<unsigned char>(trimmed[i + 1]))) {
            flush();
            while (i + 1 < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i + 1]))) ++i;
        }
    }
    if (!cur.empty()) flush();

    double avg = mean_i(sentence_lens);
    double stdv = std_i(sentence_lens, avg);
    double burst = avg == 0 ? 0 : stdv / avg;

    long punct = 0;
    static const std::string puncts = ".,;:!?\"'()-";
    for (unsigned char c : text) {
        if (puncts.find(static_cast<char>(c)) != std::string::npos) punct++;
    }
    double pr = text.empty() ? 0 : punct / static_cast<double>(text.size());

    Features f;
    f.word_count = word_count;
    f.type_token_ratio = ttr;
    f.avg_sentence_length = avg;
    f.sentence_length_std = stdv;
    f.burstiness = burst;
    f.punctuation_ratio = pr;
    f.char_entropy = entropy_of(text);
    f.repetition_score = repetition_of(words);
    return f;
}

double stylometry_ai_score(const Features& f) {
    double uniform = clamp01(1.0 - f.burstiness / 1.2);
    double repetition = clamp01(f.repetition_score);
    double sentence_uniform = clamp01(1.0 - f.sentence_length_std / 18.0);
    double entropy_low = clamp01((4.6 - f.char_entropy) / 1.5);
    return clamp01(0.30 * uniform + 0.25 * repetition + 0.25 * sentence_uniform + 0.20 * entropy_low);
}

}  // namespace kos
