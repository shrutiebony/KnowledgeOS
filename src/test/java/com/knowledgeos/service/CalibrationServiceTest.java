package com.knowledgeos.service;

import com.knowledgeos.model.ClassificationBand;
import org.junit.jupiter.api.Test;

import java.time.LocalDate;
import java.util.LinkedHashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CalibrationServiceTest {
    private final CalibrationService calibration = new CalibrationService(0.58, 0.42, 0.50, 0.38);

    @Test
    void highUniformSignalsLeanAi() {
        Map<String, Double> raw = new LinkedHashMap<>();
        raw.put("stylometry", 0.9);
        raw.put("stock_phrase_detector", 0.85);
        raw.put("sentence_uniformity_detector", 0.88);
        raw.put("ngram_repetition_detector", 0.8);
        raw.put("embedding_anomaly", 0.7);
        raw.put("stylometry_deviation", 0.6);
        CalibrationService.Estimate e = calibration.calibrate(raw);
        assertTrue(e.pAi() > 0.65);
        assertEquals(ClassificationBand.LIKELY_AI, e.band());
        assertTrue(e.ciLow() <= e.pAi());
        assertTrue(e.ciHigh() >= e.pAi());
    }

    @Test
    void variedHumanLikeSignalsLeanHuman() {
        Map<String, Double> raw = new LinkedHashMap<>();
        raw.put("stylometry", 0.15);
        raw.put("stock_phrase_detector", 0.0);
        raw.put("sentence_uniformity_detector", 0.2);
        raw.put("ngram_repetition_detector", 0.1);
        raw.put("embedding_anomaly", 0.2);
        raw.put("stylometry_deviation", 0.15);
        raw.put("post_chatgpt", 0.20);
        CalibrationService.Estimate e = calibration.calibrate(raw);
        assertTrue(e.pAi() < 0.42);
        assertEquals(ClassificationBand.LIKELY_HUMAN, e.band());
    }

    @Test
    void post2023PriorRaisesEstimateWithoutForcingAi() {
        Map<String, Double> base = new LinkedHashMap<>();
        base.put("stylometry", 0.32);
        base.put("stock_phrase_detector", 0.0);
        base.put("sentence_uniformity_detector", 0.42);
        base.put("ngram_repetition_detector", 0.15);
        base.put("embedding_anomaly", 0.5);
        base.put("stylometry_deviation", 0.2);
        Map<String, Double> old = new LinkedHashMap<>(base);
        old.put("post_chatgpt", CalibrationService.postChatgptSignal(LocalDate.of(2019, 6, 1)));
        Map<String, Double> recent = new LinkedHashMap<>(base);
        recent.put("post_chatgpt", CalibrationService.postChatgptSignal(LocalDate.of(2024, 6, 1)));
        double pOld = calibration.calibrate(old).pAi();
        double pNew = calibration.calibrate(recent).pAi();
        assertTrue(pNew > pOld);
        assertTrue(pNew < 0.75);
        assertTrue(CalibrationService.postChatgptSignal(LocalDate.of(2021, 3, 1)) < 0.30);
        assertTrue(CalibrationService.postChatgptSignal(LocalDate.of(2024, 8, 1)) > 0.50);
    }

    @Test
    void rankMixSpreadsACompressedCollection() {
        Map<String, Double> raw = new LinkedHashMap<>();
        raw.put("stylometry", 0.28);
        raw.put("stock_phrase_detector", 0.0);
        raw.put("sentence_uniformity_detector", 0.35);
        raw.put("ngram_repetition_detector", 0.12);
        raw.put("embedding_anomaly", 0.4);
        raw.put("stylometry_deviation", 0.18);
        raw.put("post_chatgpt", 0.54);
        CalibrationService.Estimate mid = calibration.calibrate(raw);
        CalibrationService.Estimate top = calibration.mixWithRank(mid, 0.92);
        CalibrationService.Estimate bottom = calibration.mixWithRank(mid, 0.08);
        assertTrue(top.pAi() > mid.pAi());
        assertTrue(bottom.pAi() < mid.pAi());
        assertTrue(top.pAi() - bottom.pAi() > 0.15);
        assertTrue(top.band() != ClassificationBand.LIKELY_HUMAN);
        assertTrue(bottom.band() != ClassificationBand.LIKELY_AI);
    }
}
