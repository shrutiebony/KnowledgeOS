package com.knowledgeos.service;

import com.knowledgeos.model.SearchResult;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

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

        float[] vector = embeddingService.generateEmbedding(query);

        String embedding = convert(vector);

        return repository.searchSimilar(embedding);
    }

    private String convert(float[] vector) {

        StringBuilder sb = new StringBuilder("[");

        for (int i = 0; i < vector.length; i++) {

            sb.append(vector[i]);

            if (i < vector.length - 1) {
                sb.append(",");
            }
        }

        sb.append("]");

        return sb.toString();
    }
}