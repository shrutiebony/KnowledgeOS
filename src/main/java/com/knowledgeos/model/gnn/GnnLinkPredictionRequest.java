package com.knowledgeos.model.gnn;

import java.util.List;

public class GnnLinkPredictionRequest {

    private int topK;
    private List<Long> nodeIds;

    public GnnLinkPredictionRequest(int topK, List<Long> nodeIds) {
        this.topK = topK;
        this.nodeIds = nodeIds;
    }

    public int getTopK() { return topK; }
    public List<Long> getNodeIds() { return nodeIds; }
}