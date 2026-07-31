package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;

@Entity
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class Document {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;    

    private String title;

    @Column(columnDefinition = "TEXT")
    private String content;

    private String url;
    public Document(String title, String url, String content) {
        this.title = title;
        this.url = url;
        this.content = content;
    }
}