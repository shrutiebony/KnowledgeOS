package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

@Service
public class ChunkingService {

    // Word-based chunking avoids slicing mid-word/mid-sentence like raw
    // character counting did. Overlap keeps context from being lost right
    // at a chunk boundary, which is what actually hurts retrieval quality.
    private static final int CHUNK_SIZE_WORDS = 200;
    private static final int OVERLAP_WORDS = 40;

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

        if (content == null || content.isBlank()) {
            System.out.println(
                    "Skipping chunking: document " + document.getId() + " has no content"
            );
            return;
        }

        List<String> words = Arrays.asList(content.trim().split("\\s+"));

        List<DocumentChunk> chunks = new ArrayList<>();
        List<String> chunkTexts = new ArrayList<>();

        int step = Math.max(1, CHUNK_SIZE_WORDS - OVERLAP_WORDS);
        int start = 0;

        while (start < words.size()) {

            int end = Math.min(start + CHUNK_SIZE_WORDS, words.size());

            String text = String.join(" ", words.subList(start, end));

            DocumentChunk chunk = new DocumentChunk();
            chunk.setDocument(document);
            chunk.setContent(text);

            chunks.add(chunk);
            chunkTexts.add(text);

            if (end == words.size()) {
                break;
            }

            start += step;
        }

        try {
            // One HTTP call for the whole document instead of one per chunk.
            List<float[]> embeddings = embeddingService.generateEmbeddingsBatch(chunkTexts);

            for (int i = 0; i < chunks.size(); i++) {
                chunks.get(i).setEmbedding(embeddings.get(i));
            }

        } catch (Exception e) {
            // Leave embeddings null - EmbeddingMigrationScheduler picks these
            // up and retries later instead of losing the whole document.
            System.out.println(
                    "Batch embedding failed for document " + document.getId() +
                    " (" + chunks.size() + " chunks): " + e.getMessage()
            );
        }

        repository.saveAll(chunks);

        System.out.println(
                "Created chunks: " + chunks.size() + " for document " + document.getId()
        );
    }
}