package com.knowledgeos.service;

import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;

@Service
public class StylometryService {
    private static final Pattern SENTENCE = Pattern.compile("(?<=[.!?])\\s+");
    private static final Pattern WORD = Pattern.compile("[A-Za-z0-9']+");

    public record Features(
            int wordCount,
            double typeTokenRatio,
            double avgSentenceLength,
            double sentenceLengthStd,
            double burstiness,
            double punctuationRatio,
            double charEntropy,
            double repetitionScore
    ) {}

    public Features analyze(String text) {
        if (text == null) {
            text = "";
        }
        List<String> words = words(text);
        int wordCount = words.size();
        double ttr = wordCount == 0 ? 0 : unique(words) / (double) wordCount;

        List<Integer> sentenceLens = new ArrayList<>();
        for (String sentence : SENTENCE.split(text.trim())) {
            int n = words(sentence).size();
            if (n > 0) {
                sentenceLens.add(n);
            }
        }
        double avg = mean(sentenceLens);
        double std = std(sentenceLens, avg);
        double burstiness = avg == 0 ? 0 : std / avg;

        long punct = text.chars().filter(c -> ".,;:!?\"'()-".indexOf(c) >= 0).count();
        double punctuationRatio = text.isEmpty() ? 0 : punct / (double) text.length();
        double entropy = entropy(text);
        double repetition = repetitionScore(words);

        return new Features(wordCount, ttr, avg, std, burstiness, punctuationRatio, entropy, repetition);
    }

    public double stylometryAiScore(Features f) {
        // Lower burstiness, mid-high TTR collapse toward formulaic, higher repetition, more uniform sentences.
        double uniform = clamp01(1.0 - f.burstiness / 1.2);
        double repetition = clamp01(f.repetitionScore);
        double sentenceUniform = clamp01(1.0 - f.sentenceLengthStd / 18.0);
        double entropyLow = clamp01((4.6 - f.charEntropy) / 1.5);
        return clamp01(0.30 * uniform + 0.25 * repetition + 0.25 * sentenceUniform + 0.20 * entropyLow);
    }

    private static List<String> words(String text) {
        List<String> out = new ArrayList<>();
        var matcher = WORD.matcher(text.toLowerCase(Locale.ROOT));
        while (matcher.find()) {
            out.add(matcher.group());
        }
        return out;
    }

    private static int unique(List<String> words) {
        return (int) words.stream().distinct().count();
    }

    private static double mean(List<Integer> values) {
        if (values.isEmpty()) {
            return 0;
        }
        return values.stream().mapToInt(Integer::intValue).average().orElse(0);
    }

    private static double std(List<Integer> values, double mean) {
        if (values.size() < 2) {
            return 0;
        }
        double var = 0;
        for (int v : values) {
            double d = v - mean;
            var += d * d;
        }
        return Math.sqrt(var / (values.size() - 1));
    }

    private static double entropy(String text) {
        if (text.isEmpty()) {
            return 0;
        }
        int[] counts = new int[256];
        int n = 0;
        for (char c : text.toCharArray()) {
            if (c < 256) {
                counts[c]++;
                n++;
            }
        }
        double h = 0;
        for (int count : counts) {
            if (count == 0) {
                continue;
            }
            double p = count / (double) n;
            h -= p * (Math.log(p) / Math.log(2));
        }
        return h;
    }

    private static double repetitionScore(List<String> words) {
        if (words.size() < 6) {
            return 0;
        }
        Map<String, Integer> trigrams = new HashMap<>();
        int total = 0;
        for (int i = 0; i < words.size() - 2; i++) {
            String g = words.get(i) + " " + words.get(i + 1) + " " + words.get(i + 2);
            trigrams.merge(g, 1, Integer::sum);
            total++;
        }
        int repeats = 0;
        for (int c : trigrams.values()) {
            if (c > 1) {
                repeats += c - 1;
            }
        }
        return clamp01(repeats / (double) Math.max(1, total));
    }

    public static double clamp01(double v) {
        return Math.max(0, Math.min(1, v));
    }
}
