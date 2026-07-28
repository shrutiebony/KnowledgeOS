package com.knowledgeos.repository;


import com.knowledgeos.model.DocumentChunk;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;


import java.util.List;



public interface DocumentChunkRepository
        extends JpaRepository<DocumentChunk, Long> {



    @Query(value = """
        SELECT *
        FROM document_chunk
        ORDER BY embedding <-> CAST(:embedding AS vector)
        LIMIT 5
        """,
            nativeQuery = true)
    List<DocumentChunk> findSimilarChunks(float[] embedding);


}