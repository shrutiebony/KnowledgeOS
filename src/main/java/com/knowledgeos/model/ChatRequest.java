package com.knowledgeos.model;

public class ChatRequest {

    private String query;

    public ChatRequest() {}

    public ChatRequest(String query) {
        this.query = query;
    }

    public String getQuery() {
        return query;
    }

    public void setQuery(String query) {
        this.query = query;
    }
}