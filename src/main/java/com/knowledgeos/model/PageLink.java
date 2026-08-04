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

    // Stored as a URL rather than a Document reference because the target
    // may not be crawled yet (or ever) - HITS computation filters this down
    // at query time to edges where both endpoints exist in the corpus.
    @Column(nullable = false)
    private String targetUrl;

    public PageLink(Document source, String targetUrl) {
        this.source = source;
        this.targetUrl = targetUrl;
    }
}
