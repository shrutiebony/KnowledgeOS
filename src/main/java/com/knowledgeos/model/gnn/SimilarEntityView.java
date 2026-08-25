package com.knowledgeos.model.gnn;

public class SimilarEntityView {

    private Long entityId;
    private String name;
    private String type;

    public SimilarEntityView(Long entityId, String name, String type) {
        this.entityId = entityId;
        this.name = name;
        this.type = type;
    }

    public Long getEntityId() { return entityId; }
    public String getName() { return name; }
    public String getType() { return type; }
}