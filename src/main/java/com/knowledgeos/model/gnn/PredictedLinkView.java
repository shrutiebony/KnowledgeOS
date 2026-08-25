package com.knowledgeos.model.gnn;

public class PredictedLinkView {

    private Long sourceId;
    private String sourceName;
    private Long targetId;
    private String targetName;
    private double score;

    public PredictedLinkView(Long sourceId, String sourceName, Long targetId, String targetName, double score) {
        this.sourceId = sourceId;
        this.sourceName = sourceName;
        this.targetId = targetId;
        this.targetName = targetName;
        this.score = score;
    }

    public Long getSourceId() { return sourceId; }
    public String getSourceName() { return sourceName; }
    public Long getTargetId() { return targetId; }
    public String getTargetName() { return targetName; }
    public double getScore() { return score; }
}