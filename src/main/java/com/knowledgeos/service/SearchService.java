package com.knowledgeos.service;

import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.model.SearchResult;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

@Service
public class SearchService {

    private final DocumentChunkRepository repository;

    private final EmbeddingService embeddingService;

    public SearchService(
            DocumentChunkRepository repository,
            EmbeddingService embeddingService
    ) {
        this.repository = repository;
        this.embeddingService = embeddingService;
    }

    public List<SearchResult> search(String query) {

        float[] embedding =
                embeddingService.generateEmbedding(query);

        List<DocumentChunk> chunks =
                repository.findSimilarChunks(embedding);

        List<SearchResult> results = new ArrayList<>();

        for (DocumentChunk chunk : chunks) {

            results.add(
                    new SearchResult(
                            chunk.getDocument().getTitle(),
                            chunk.getDocument().getUrl(),
                            chunk.getContent()
                    )
            );
        }

        return results;
    }
}