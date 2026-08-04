package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;
import org.hibernate.annotations.CreationTimestamp;

import java.time.Instant;

@Entity
@Getter
@Setter
@NoArgsConstructor
public class Document {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;    

    private String title;

    @Column(columnDefinition = "TEXT")
    private String content;

    private String url;

    @CreationTimestamp
    @Column(updatable = false)
    private Instant createdAt;

    private boolean entitiesExtracted = false;

    private Double hubScore;
    private Double authorityScore;

    // Generic content-analysis pipeline. aiTextScore is a rough estimate
    // (0-1, from an LLM-judge heuristic - NOT a validated detector) of how
    // likely the content is AI-generated/assisted. Stylometric features are
    // computed directly and can be reused for other analysis tasks beyond
    // AI-text detection, since they're generic writing-style signals.
    private boolean contentAnalyzed = false;
    private Double aiTextScore;
    private Double typeTokenRatio;
    private Double avgSentenceLength;
    private Double sentenceLengthVariance;

    public Document(String title, String url, String content) {
        this.title = title;
        this.url = url;
        this.content = content;
    }
}
