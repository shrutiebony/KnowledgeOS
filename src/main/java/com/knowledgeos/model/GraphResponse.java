package com.knowledgeos.model;

import java.util.List;

public class GraphResponse {

    private List<GraphNode> nodes;
    private List<GraphEdge> edges;

    public GraphResponse(List<GraphNode> nodes, List<GraphEdge> edges) {
        this.nodes = nodes;
        this.edges = edges;
    }

    public List<GraphNode> getNodes() { return nodes; }
    public List<GraphEdge> getEdges() { return edges; }
}
