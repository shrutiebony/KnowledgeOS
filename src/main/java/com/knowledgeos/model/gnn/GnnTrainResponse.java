package com.knowledgeos.model.gnn;

public class GnnTrainResponse {

    private int nodesTrained;
    private int edgesTrained;
    private int epochs;
    private double finalLoss;
    private int embeddingDim;

    public int getNodesTrained() { return nodesTrained; }
    public void setNodesTrained(int nodesTrained) { this.nodesTrained = nodesTrained; }

    public int getEdgesTrained() { return edgesTrained; }
    public void setEdgesTrained(int edgesTrained) { this.edgesTrained = edgesTrained; }

    public int getEpochs() { return epochs; }
    public void setEpochs(int epochs) { this.epochs = epochs; }

    public double getFinalLoss() { return finalLoss; }
    public void setFinalLoss(double finalLoss) { this.finalLoss = finalLoss; }

    public int getEmbeddingDim() { return embeddingDim; }
    public void setEmbeddingDim(int embeddingDim) { this.embeddingDim = embeddingDim; }
}