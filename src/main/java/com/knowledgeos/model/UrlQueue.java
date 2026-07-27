package com.knowledgeos.model;

import jakarta.persistence.*;

@Entity
@Table(name = "url_queue")
public class UrlQueue {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    private String url;

    private boolean visited = false;


    public Long getId() {
        return id;
    }


    public String getUrl() {
        return url;
    }


    public void setUrl(String url) {
        this.url = url;
    }


    public boolean isVisited() {
        return visited;
    }


    public void setVisited(boolean visited) {
        this.visited = visited;
    }
}