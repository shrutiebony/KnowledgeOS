package com.knowledgeos.model;

import java.util.List;

public class ChatResponse {

    private String answer;

    private List<String> sources;


    public ChatResponse(
            String answer,
            List<String> sources
    ) {
        this.answer = answer;
        this.sources = sources;
    }


    public String getAnswer() {
        return answer;
    }


    public List<String> getSources() {
        return sources;
    }
}