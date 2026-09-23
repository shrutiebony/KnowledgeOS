package com.knowledgeos.model;

import java.time.LocalDate;

/**
 * Graph / summary / breakdown row without article text.
 */
public record DocumentMetaView(
        Long id,
        Long datasetId,
        String title,
        String url,
        String source,
        String topic,
        LocalDate publishedAt,
        int wordCount,
        Double pAi,
        Double ciLow,
        Double ciHigh,
        ClassificationBand band
) {}
