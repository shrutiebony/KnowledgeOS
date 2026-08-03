package com.knowledgeos.model;

import java.util.List;

public class EmbeddingBatchRequest {

    private List<String> texts;


    public EmbeddingBatchRequest(List<String> texts) {
        this.texts = texts;
    }


    public List<String> getTexts() {
        return texts;
    }


    public void setTexts(List<String> texts) {
        this.texts = texts;
    }
}