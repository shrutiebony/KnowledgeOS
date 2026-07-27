package com.knowledgeos.model;

import jakarta.persistence.*;

@Entity
public class DocumentChunk {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne
    private Document document;

    @Column(columnDefinition = "TEXT")
    private String content;


    public Long getId() {
        return id;
    }

    public Document getDocument() {
        return document;
    }

    public String getContent() {
        return content;
    }


    public void setDocument(Document document) {
        this.document = document;
    }

    public void setContent(String content) {
        this.content = content;
    }
}