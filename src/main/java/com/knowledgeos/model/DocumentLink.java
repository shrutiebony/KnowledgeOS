package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;

@Entity
@Getter
@Setter
public class DocumentLink {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne
    @JoinColumn(name="source_document_id")
    private Document source;

    @ManyToOne
    @JoinColumn(name="target_document_id")
    private Document target;
}