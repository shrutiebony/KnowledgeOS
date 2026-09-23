package com.knowledgeos.model;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Table;

import java.time.Instant;

@Entity
@Table(name = "datasets")
public class Dataset {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private String name;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false)
    private DatasetKind kind;

    private String parentTopic;
    private Long parentDatasetId;

    @Enumerated(EnumType.STRING)
    private GraphSageStatus graphSageStatus = GraphSageStatus.NOT_TRAINED;

    private String graphSageMessage = "GraphSAGE representations may be computed as graph context. They are not a detection signal until a labeled classifier is trained and validated.";

    private Instant createdAt = Instant.now();
    private Instant lastAnalyzedAt;
    private String analysisState = "idle";

    public Long getId() {
        return id;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public DatasetKind getKind() {
        return kind;
    }

    public void setKind(DatasetKind kind) {
        this.kind = kind;
    }

    public String getParentTopic() {
        return parentTopic;
    }

    public void setParentTopic(String parentTopic) {
        this.parentTopic = parentTopic;
    }

    public Long getParentDatasetId() {
        return parentDatasetId;
    }

    public void setParentDatasetId(Long parentDatasetId) {
        this.parentDatasetId = parentDatasetId;
    }

    public GraphSageStatus getGraphSageStatus() {
        return graphSageStatus;
    }

    public void setGraphSageStatus(GraphSageStatus graphSageStatus) {
        this.graphSageStatus = graphSageStatus;
    }

    public String getGraphSageMessage() {
        return graphSageMessage;
    }

    public void setGraphSageMessage(String graphSageMessage) {
        this.graphSageMessage = graphSageMessage;
    }

    public Instant getCreatedAt() {
        return createdAt;
    }

    public void setCreatedAt(Instant createdAt) {
        this.createdAt = createdAt;
    }

    public Instant getLastAnalyzedAt() {
        return lastAnalyzedAt;
    }

    public void setLastAnalyzedAt(Instant lastAnalyzedAt) {
        this.lastAnalyzedAt = lastAnalyzedAt;
    }

    public String getAnalysisState() {
        return analysisState;
    }

    public void setAnalysisState(String analysisState) {
        this.analysisState = analysisState;
    }
}
