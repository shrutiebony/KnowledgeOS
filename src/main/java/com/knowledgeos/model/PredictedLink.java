package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

import java.time.Instant;

@Entity
@Table(name = "predicted_link")
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class PredictedLink {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "source_entity_id")
    private GraphEntity source;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "target_entity_id")
    private GraphEntity target;
    private double score;

    @Column(name = "created_at")
    private Instant createdAt;

    public PredictedLink(GraphEntity source, GraphEntity target, double score) {
        this.source = source;
        this.target = target;
        this.score = score;
        this.createdAt = Instant.now();
    }
}