package com.knowledgeos.model;

import jakarta.persistence.Column;
import jakarta.persistence.Convert;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Lob;
import jakarta.persistence.Table;

import java.time.LocalDate;

@Entity
@Table(name = "documents")
public class Document {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private Long datasetId;

    @Column(nullable = false)
    private String title;

    @Column(length = 2000)
    private String url;

    @Column(length = 2000)
    private String source;
    private String topic;
    private LocalDate publishedAt;

    @Lob
    @Column(nullable = false)
    private String text;

    private int wordCount;

    @Convert(converter = FloatArrayConverter.class)
    @Column(length = 8000)
    private float[] embedding;

    @Convert(converter = FloatArrayConverter.class)
    @Column(length = 8000)
    private float[] gnnEmbedding;

    private Double typeTokenRatio;
    private Double avgSentenceLength;
    private Double sentenceLengthStd;
    private Double burstiness;
    private Double punctuationRatio;
    private Double charEntropy;
    private Double repetitionScore;

    private Double detectorStylometry;
    private Double detectorRepetition;
    private Double detectorUniformity;
    private Double embeddingAnomaly;
    private Double stylometryDeviation;

    private Double pAi;
    private Double ciLow;
    private Double ciHigh;

    @Enumerated(EnumType.STRING)
    private ClassificationBand band = ClassificationBand.PENDING;

    @Column(length = 4000)
    private String explanationJson;

    public Long getId() {
        return id;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public Long getDatasetId() {
        return datasetId;
    }

    public void setDatasetId(Long datasetId) {
        this.datasetId = datasetId;
    }

    public String getTitle() {
        return title;
    }

    public void setTitle(String title) {
        this.title = title;
    }

    public String getUrl() {
        return url;
    }

    public void setUrl(String url) {
        this.url = url;
    }

    public String getSource() {
        return source;
    }

    public void setSource(String source) {
        this.source = source;
    }

    public String getTopic() {
        return topic;
    }

    public void setTopic(String topic) {
        this.topic = topic;
    }

    public LocalDate getPublishedAt() {
        return publishedAt;
    }

    public void setPublishedAt(LocalDate publishedAt) {
        this.publishedAt = publishedAt;
    }

    public String getText() {
        return text;
    }

    public void setText(String text) {
        this.text = text;
    }

    public int getWordCount() {
        return wordCount;
    }

    public void setWordCount(int wordCount) {
        this.wordCount = wordCount;
    }

    public float[] getEmbedding() {
        return embedding;
    }

    public void setEmbedding(float[] embedding) {
        this.embedding = embedding;
    }

    public float[] getGnnEmbedding() {
        return gnnEmbedding;
    }

    public void setGnnEmbedding(float[] gnnEmbedding) {
        this.gnnEmbedding = gnnEmbedding;
    }

    public Double getTypeTokenRatio() {
        return typeTokenRatio;
    }

    public void setTypeTokenRatio(Double typeTokenRatio) {
        this.typeTokenRatio = typeTokenRatio;
    }

    public Double getAvgSentenceLength() {
        return avgSentenceLength;
    }

    public void setAvgSentenceLength(Double avgSentenceLength) {
        this.avgSentenceLength = avgSentenceLength;
    }

    public Double getSentenceLengthStd() {
        return sentenceLengthStd;
    }

    public void setSentenceLengthStd(Double sentenceLengthStd) {
        this.sentenceLengthStd = sentenceLengthStd;
    }

    public Double getBurstiness() {
        return burstiness;
    }

    public void setBurstiness(Double burstiness) {
        this.burstiness = burstiness;
    }

    public Double getPunctuationRatio() {
        return punctuationRatio;
    }

    public void setPunctuationRatio(Double punctuationRatio) {
        this.punctuationRatio = punctuationRatio;
    }

    public Double getCharEntropy() {
        return charEntropy;
    }

    public void setCharEntropy(Double charEntropy) {
        this.charEntropy = charEntropy;
    }

    public Double getRepetitionScore() {
        return repetitionScore;
    }

    public void setRepetitionScore(Double repetitionScore) {
        this.repetitionScore = repetitionScore;
    }

    public Double getDetectorStylometry() {
        return detectorStylometry;
    }

    public void setDetectorStylometry(Double detectorStylometry) {
        this.detectorStylometry = detectorStylometry;
    }

    public Double getDetectorRepetition() {
        return detectorRepetition;
    }

    public void setDetectorRepetition(Double detectorRepetition) {
        this.detectorRepetition = detectorRepetition;
    }

    public Double getDetectorUniformity() {
        return detectorUniformity;
    }

    public void setDetectorUniformity(Double detectorUniformity) {
        this.detectorUniformity = detectorUniformity;
    }

    public Double getEmbeddingAnomaly() {
        return embeddingAnomaly;
    }

    public void setEmbeddingAnomaly(Double embeddingAnomaly) {
        this.embeddingAnomaly = embeddingAnomaly;
    }

    public Double getStylometryDeviation() {
        return stylometryDeviation;
    }

    public void setStylometryDeviation(Double stylometryDeviation) {
        this.stylometryDeviation = stylometryDeviation;
    }

    public Double getPAi() {
        return pAi;
    }

    public void setPAi(Double pAi) {
        this.pAi = pAi;
    }

    public Double getCiLow() {
        return ciLow;
    }

    public void setCiLow(Double ciLow) {
        this.ciLow = ciLow;
    }

    public Double getCiHigh() {
        return ciHigh;
    }

    public void setCiHigh(Double ciHigh) {
        this.ciHigh = ciHigh;
    }

    public ClassificationBand getBand() {
        return band;
    }

    public void setBand(ClassificationBand band) {
        this.band = band;
    }

    public String getExplanationJson() {
        return explanationJson;
    }

    public void setExplanationJson(String explanationJson) {
        this.explanationJson = explanationJson;
    }
}
