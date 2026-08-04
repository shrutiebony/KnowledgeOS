package com.knowledgeos.service;

import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;


@Service
public class StylometryService {

    public record StylometricFeatures(
            double typeTokenRatio,
            double avgSentenceLength,
            double sentenceLengthVariance
    ) {}

    public StylometricFeatures computeFeatures(String content) {

        String[] words = content.toLowerCase()
                .replaceAll("[^a-z0-9\\s]", " ")
                .trim()
                .split("\\s+");

        double typeTokenRatio = 0;

        if (words.length > 0) {
            long uniqueWords = Arrays.stream(words).distinct().count();
            typeTokenRatio = (double) uniqueWords / words.length;
        }

        String[] rawSentences = content.split("[.!?]+");

        List<Integer> sentenceLengths = new ArrayList<>();
        for (String sentence : rawSentences) {
            if (!sentence.isBlank()) {
                sentenceLengths.add(sentence.trim().split("\\s+").length);
            }
        }

        double avgSentenceLength = 0;
        double sentenceLengthVariance = 0;

        if (!sentenceLengths.isEmpty()) {

            avgSentenceLength = sentenceLengths.stream()
                    .mapToInt(Integer::intValue)
                    .average()
                    .orElse(0);

            double mean = avgSentenceLength;

            sentenceLengthVariance = sentenceLengths.stream()
                    .mapToDouble(len -> Math.pow(len - mean, 2))
                    .average()
                    .orElse(0);
        }

        return new StylometricFeatures(
                typeTokenRatio,
                avgSentenceLength,
                sentenceLengthVariance
        );
    }
}
