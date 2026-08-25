package com.knowledgeos.repository;

import com.knowledgeos.model.GnnEmbedding;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;
import java.util.Optional;

public interface GnnEmbeddingRepository extends JpaRepository<GnnEmbedding, Long> {

    Optional<GnnEmbedding> findByEntityId(Long entityId);

    @Modifying
    @Transactional
    void deleteByEntityId(Long entityId);


    @Query(value = """
            SELECT
                ge.id,
                ge.entity_id,
                ge.embedding
            FROM gnn_embedding ge
            WHERE ge.entity_id <> :excludeEntityId
            ORDER BY ge.embedding <-> CAST(:embedding AS vector)
            LIMIT :limit
            """,
            nativeQuery = true)
    List<GnnEmbedding> searchSimilar(
            @Param("embedding") String embedding,
            @Param("excludeEntityId") Long excludeEntityId,
            @Param("limit") int limit
    );
}