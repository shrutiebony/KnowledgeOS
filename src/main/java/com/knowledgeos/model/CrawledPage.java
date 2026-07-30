package com.knowledgeos.model;

public class CrawledPage {

    private final String title;
    private final String content;

    public CrawledPage(String title, String content){
        this.title = title;
        this.content = content;
    }

    public String getTitle(){
        return title;
    }

    public String getContent(){
        return content;
    }
}