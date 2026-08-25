package com.knowledgeos.model.gnn;

public class GnnEdgeDto {

    private Long source;
    private Long target;

    public GnnEdgeDto(Long source, Long target) {
        this.source = source;
        this.target = target;
    }

    public Long getSource() { return source; }
    public Long getTarget() { return target; }
}