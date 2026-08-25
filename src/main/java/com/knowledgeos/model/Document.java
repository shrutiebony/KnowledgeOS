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
    @Column(nullable = false)
    private String datasetKey = "default";

    @CreationTimestamp
    @Column(updatable = false)
    private Instant createdAt;

    private boolean entitiesExtracted = false;

    private Double hubScore;
    private Double authorityScore;

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

    public Document(String title, String url, String content, String datasetKey) {
        this.title = title;
        this.url = url;
        this.content = content;
        this.datasetKey = datasetKey;
    }
}