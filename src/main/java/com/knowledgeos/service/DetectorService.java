package com.knowledgeos.service;

import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Local detectors. Additional commercial detectors can implement the same
 * 0–1 AI-likelihood contract and be blended in CalibrationService.
 */
@Service
public class DetectorService {
    public interface Detector {
        String name();
        double score(String text, StylometryService.Features features);
    }

    private static final Pattern WORD = Pattern.compile("[A-Za-z']+");
    private static final String[] AI_MARKERS = {
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

    private final List<Detector> detectors;

    public DetectorService() {
        this.detectors = List.of(new MarkerDetector(), new UniformityDetector(), new RepetitionDetector());
    }

    public List<Detector> detectors() {
        return detectors;
    }

    static class MarkerDetector implements Detector {
        @Override
        public String name() {
            return "stock_phrase_detector";
        }

        @Override
        public double score(String text, StylometryService.Features features) {
            String lower = text == null ? "" : text.toLowerCase(Locale.ROOT);
            int hits = 0;
            for (String marker : AI_MARKERS) {
                if (lower.contains(marker)) {
                    hits++;
                }
            }
            return StylometryService.clamp01(hits / 4.0);
        }
    }

    static class UniformityDetector implements Detector {
        @Override
        public String name() {
            return "sentence_uniformity_detector";
        }

        @Override
        public double score(String text, StylometryService.Features features) {
            double burst = features == null ? 0.8 : features.burstiness();
            double std = features == null ? 12 : features.sentenceLengthStd();
            return StylometryService.clamp01(0.6 * (1.0 - burst / 1.1) + 0.4 * (1.0 - std / 16.0));
        }
    }

    static class RepetitionDetector implements Detector {
        @Override
        public String name() {
            return "ngram_repetition_detector";
        }

        @Override
        public double score(String text, StylometryService.Features features) {
            if (features != null) {
                return StylometryService.clamp01(features.repetitionScore() * 1.4);
            }
            return 0;
        }
    }

    public static List<String> tokens(String text) {
        List<String> out = new ArrayList<>();
        var matcher = WORD.matcher(text == null ? "" : text);
        while (matcher.find()) {
            out.add(matcher.group());
        }
        return out;
    }
}
