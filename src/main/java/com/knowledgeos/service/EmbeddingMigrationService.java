package com.knowledgeos.service;

import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class EmbeddingMigrationService {


    private final DocumentChunkRepository repository;
    private final EmbeddingService embeddingService;


    public EmbeddingMigrationService(
            DocumentChunkRepository repository,
            EmbeddingService embeddingService
    ) {
        this.repository = repository;
        this.embeddingService = embeddingService;
    }


    public void generateMissingEmbeddings() {


        List<DocumentChunk> chunks =
                repository.findByEmbeddingIsNull();


        for (DocumentChunk chunk : chunks) {


            float[] embedding =
                    embeddingService.generateEmbedding(
                            chunk.getContent()
                    );


            chunk.setEmbedding(embedding);

        }


        repository.saveAll(chunks);


    }
}