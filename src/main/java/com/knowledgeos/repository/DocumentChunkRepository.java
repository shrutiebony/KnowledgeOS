package com.knowledgeos.repository;

import com.knowledgeos.model.DocumentChunk;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.List;


public interface DocumentChunkRepository
        extends JpaRepository<DocumentChunk, Long> {


    List<DocumentChunk> findByEmbeddingIsNull();


    long countByEmbeddingIsNull();


    @Query(value = """
            SELECT 
                dc.id,
                dc.content,
                dc.document_id,
                dc.embedding
            FROM document_chunk dc
            ORDER BY dc.embedding <-> CAST(:embedding AS vector)
            LIMIT 20
            """,
            nativeQuery = true)
    List<DocumentChunk> searchSimilar(
            @Param("embedding") String embedding
    );

}