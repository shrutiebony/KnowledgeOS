package com.knowledgeos.model.gnn;

import java.util.List;

public class GnnTrainRequest {

    private List<GnnNodeDto> nodes;
    private List<GnnEdgeDto> edges;

    public GnnTrainRequest(List<GnnNodeDto> nodes, List<GnnEdgeDto> edges) {
        this.nodes = nodes;
        this.edges = edges;
    }

    public List<GnnNodeDto> getNodes() { return nodes; }
    public List<GnnEdgeDto> getEdges() { return edges; }
}