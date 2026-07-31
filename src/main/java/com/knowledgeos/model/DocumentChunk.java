package com.knowledgeos.model;


import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;


@Entity
@Getter
@Setter
public class DocumentChunk {


    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;


    @ManyToOne
    private Document document;


    @Column(columnDefinition = "TEXT")
    private String content;


}