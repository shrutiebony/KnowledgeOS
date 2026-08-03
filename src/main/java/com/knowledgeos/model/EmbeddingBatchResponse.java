package com.knowledgeos.model;

import java.util.List;

public class EmbeddingBatchResponse {

    private List<List<Float>> embeddings;


    public List<List<Float>> getEmbeddings() {
        return embeddings;
    }


    public void setEmbeddings(List<List<Float>> embeddings) {
        this.embeddings = embeddings;
    }
}