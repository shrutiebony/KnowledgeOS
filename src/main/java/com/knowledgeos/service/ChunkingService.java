package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

@Service
public class ChunkingService {

    private final DocumentChunkRepository repository;
    private final EmbeddingService embeddingService;

    public ChunkingService(
            DocumentChunkRepository repository,
            EmbeddingService embeddingService
    ) {
        this.repository = repository;
        this.embeddingService = embeddingService;
    }

    public void chunk(Document document) {

        String content = document.getContent();

        int chunkSize = 500;

        List<DocumentChunk> chunks = new ArrayList<>();

        for (int i = 0; i < content.length(); i += chunkSize) {

            int end = Math.min(
                    i + chunkSize,
                    content.length()
            );

            String text = content.substring(i, end);

            DocumentChunk chunk = new DocumentChunk();

            chunk.setDocument(document);

            chunk.setContent(text);

            float[] embedding =
                    embeddingService.generateEmbedding(text);

            chunk.setEmbedding(embedding);

            chunks.add(chunk);
        }

        repository.saveAll(chunks);

        System.out.println(
                "Created chunks: " + chunks.size()
        );
    }
}