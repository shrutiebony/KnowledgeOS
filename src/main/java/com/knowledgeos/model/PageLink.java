package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;

@Entity
@Table(name = "page_link")
@Getter
@Setter
@NoArgsConstructor
public class PageLink {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "source_document_id", nullable = false)
    private Document source;

    @Column(nullable = false)
    private String targetUrl;

    public PageLink(Document source, String targetUrl) {
        this.source = source;
        this.targetUrl = targetUrl;
    }
}