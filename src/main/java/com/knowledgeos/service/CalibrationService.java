package com.knowledgeos.service;

import com.knowledgeos.model.ClassificationBand;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.time.LocalDate;
import java.util.LinkedHashMap;
import java.util.Map;

@Service
public class CalibrationService {
    public static final LocalDate CHATGPT_LAUNCH = LocalDate.of(2022, 11, 30);

    private final double aiMin;
    private final double humanMax;
    private final double maxInterval;
    private final double rankMix;

    public CalibrationService(
            @Value("${knowledgeos.band.ai-min:0.58}") double aiMin,
            @Value("${knowledgeos.band.human-max:0.42}") double humanMax,
            @Value("${knowledgeos.band.max-interval:0.50}") double maxInterval,
            @Value("${knowledgeos.calibrate.rank-mix:0.38}") double rankMix) {
        this.aiMin = aiMin;
        this.humanMax = humanMax;
        this.maxInterval = maxInterval;
        this.rankMix = StylometryService.clamp01(rankMix);
    }

    public record Estimate(double pAi, double ciLow, double ciHigh, ClassificationBand band, Map<String, Double> signals) {}

    public double aiMin() {
        return aiMin;
    }

    public double humanMax() {
        return humanMax;
    }

    public double maxInterval() {
        return maxInterval;
    }

    public double rankMix() {
        return rankMix;
    }

    /**
     * Modest time prior, not a hard rule. ChatGPT launched November 2022.
     * Pre-2023 leans human; 2023+ is only somewhat more likely AI-assisted.
     */
    public static double postChatgptSignal(LocalDate publishedAt) {
        if (publishedAt == null) {
            return 0.45;
        }
        if (publishedAt.isBefore(LocalDate.of(2022, 1, 1))) {
            return 0.20;
        }
        if (publishedAt.isBefore(CHATGPT_LAUNCH)) {
            return 0.26;
        }
        if (publishedAt.getYear() == 2022) {
            return 0.48;
        }
        int year = publishedAt.getYear();
        double base = 0.54 + 0.05 * Math.min(4, year - 2023);
        double month = publishedAt.getMonthValue() / 12.0 * 0.04;
        return StylometryService.clamp01(base + month);
    }

    public Estimate calibrate(Map<String, Double> rawSignals) {
        Map<String, Double> signals = new LinkedHashMap<>();
        for (Map.Entry<String, Double> e : rawSignals.entrySet()) {
            if (e.getValue() != null && !Double.isNaN(e.getValue())) {
                signals.put(e.getKey(), StylometryService.clamp01(e.getValue()));
            }
        }
        // Weighted logistic blend of local signals. GraphSAGE is intentionally omitted.
        // Intercept 0.58 is a milder human prior than 0.05 so a uniform encyclopedia
        // does not collapse to p≈0.11.
        double stylometry = signals.getOrDefault("stylometry", 0.5);
        double detectorA = signals.getOrDefault("stock_phrase_detector", 0.5);
        double detectorB = signals.getOrDefault("sentence_uniformity_detector", 0.5);
        double detectorC = signals.getOrDefault("ngram_repetition_detector", 0.5);
        double anomaly = signals.getOrDefault("embedding_anomaly", 0.5);
        double deviation = signals.getOrDefault("stylometry_deviation", 0.5);
        double recency = signals.getOrDefault("post_chatgpt", 0.45);

        double z = 0.58
                + 0.70 * (stylometry - 0.5)
                + 1.10 * (detectorA - 0.5)
                + 0.85 * (detectorB - 0.5)
                + 0.28 * (detectorC - 0.5)
                + 0.50 * (anomaly - 0.5)
                + 0.30 * (deviation - 0.5)
                + 1.15 * (recency - 0.45);
        double p = sigmoid(z);

        double[] core = {stylometry, detectorB, recency};
        double mean = 0;
        for (double v : core) {
            mean += v;
        }
        mean /= core.length;
        double var = 0;
        for (double v : core) {
            var += (v - mean) * (v - mean);
        }
        double disagreement = Math.sqrt(var / core.length);
        double half = Math.min(0.22, 0.06 + 0.45 * disagreement);
        double ciLow = StylometryService.clamp01(p - half);
        double ciHigh = StylometryService.clamp01(p + half);
        ClassificationBand band = band(p, ciHigh - ciLow);
        return new Estimate(p, ciLow, ciHigh, band, signals);
    }

    /**
     * Blend raw p(AI) with collection rank so a compressed corpus (mean 0.11)
     * is not labeled 100% human. Rank comes from the same signals, not a coin flip.
     */
    public Estimate mixWithRank(Estimate raw, double rank01) {
        double mixed = StylometryService.clamp01((1.0 - rankMix) * raw.pAi() + rankMix * StylometryService.clamp01(rank01));
        double half = Math.max(0.05, (raw.ciHigh() - raw.ciLow()) / 2.0);
        double ciLow = StylometryService.clamp01(mixed - half);
        double ciHigh = StylometryService.clamp01(mixed + half);
        return new Estimate(mixed, ciLow, ciHigh, band(mixed, ciHigh - ciLow), raw.signals());
    }

    public ClassificationBand band(double pAi, double interval) {
        if (interval > maxInterval) {
            return ClassificationBand.UNCERTAIN;
        }
        if (pAi >= aiMin) {
            return ClassificationBand.LIKELY_AI;
        }
        if (pAi <= humanMax) {
            return ClassificationBand.LIKELY_HUMAN;
        }
        return ClassificationBand.UNCERTAIN;
    }

    private static double sigmoid(double z) {
        return 1.0 / (1.0 + Math.exp(-z));
    }
}
