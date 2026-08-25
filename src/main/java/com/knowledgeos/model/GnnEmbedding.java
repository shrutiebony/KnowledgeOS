package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;

@Entity
@Table(name = "gnn_embedding")
@Getter
@Setter
public class GnnEmbedding {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    @Column(name = "entity_id", nullable = false, unique = true)
    private Long entityId;

    @JdbcTypeCode(SqlTypes.VECTOR)
    @Column(columnDefinition = "vector(64)")
    private float[] embedding;
}