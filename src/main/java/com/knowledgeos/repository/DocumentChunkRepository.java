package com.knowledgeos.repository;

import com.knowledgeos.model.SearchResult;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import com.knowledgeos.model.DocumentChunk;

import java.util.List;

public interface DocumentChunkRepository extends JpaRepository<DocumentChunk, Long> {


    @Query(value = """
            SELECT 
                dc.id AS id,
                dc.document_id AS documentId,
                d.title AS title,
                LEFT(dc.content, 300) AS snippet
            FROM document_chunk dc
            JOIN document d 
            ON dc.document_id = d.id
            WHERE dc.id IN (
                SELECT DISTINCT ON (document_id) id
                FROM document_chunk
                ORDER BY 
                    document_id,
                    embedding <-> CAST(:embedding AS vector)
            )
            ORDER BY 
                dc.embedding <-> CAST(:embedding AS vector)
            LIMIT 5
            """, nativeQuery = true)
    List<SearchResult> searchSimilar(
            @Param("embedding") String embedding
    );

}