package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;
import org.hibernate.annotations.CreationTimestamp;

import java.time.Instant;

@Entity
@Getter
@Setter
@NoArgsConstructor
public class Document {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    private String title;

    @Column(columnDefinition = "TEXT")
    private String content;

    private String url;

    @CreationTimestamp
    @Column(updatable = false)
    private Instant createdAt;

    public Document(String title, String url, String content) {
        this.title = title;
        this.url = url;
        this.content = content;
    }
}