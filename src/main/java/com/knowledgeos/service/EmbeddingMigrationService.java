package com.knowledgeos.service;

import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

@Service
public class EmbeddingMigrationService {

    private static final int BATCH_SIZE = 32;

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


        System.out.println(
                "Missing embeddings: " + chunks.size()
        );


        int succeeded = 0;
        int failed = 0;

        for (int i = 0; i < chunks.size(); i += BATCH_SIZE) {

            List<DocumentChunk> batch =
                    chunks.subList(i, Math.min(i + BATCH_SIZE, chunks.size()));

            List<String> texts = new ArrayList<>(batch.size());
            for (DocumentChunk chunk : batch) {
                texts.add(chunk.getContent());
            }

            try {

                List<float[]> embeddings = embeddingService.generateEmbeddingsBatch(texts);

                for (int j = 0; j < batch.size(); j++) {
                    batch.get(j).setEmbedding(embeddings.get(j));
                }

                repository.saveAll(batch);

                succeeded += batch.size();

            } catch (Exception e) {

                failed += batch.size();

                System.out.println(
                        "Failed to embed batch starting at index " + i +
                                " (" + batch.size() + " chunks): " + e.getMessage()
                );
            }

        }


        System.out.println(
                "Embedding migration completed. Succeeded: " + succeeded + ", Failed: " + failed
        );
    }
}