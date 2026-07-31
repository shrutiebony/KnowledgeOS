package com.knowledgeos.model;

import java.util.List;

public class EmbeddingResponse {

    private List<Float> embedding;


    public List<Float> getEmbedding() {
        return embedding;
    }


    public void setEmbedding(List<Float> embedding) {
        this.embedding = embedding;
    }
}