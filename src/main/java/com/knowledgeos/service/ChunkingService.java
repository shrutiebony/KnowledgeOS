package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;

import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;

@Service
public class ChunkingService {

    private final DocumentChunkRepository chunkRepository;
    private final EmbeddingClient embeddingClient;


    public ChunkingService(
            DocumentChunkRepository chunkRepository,
            EmbeddingClient embeddingClient
    ) {
        this.chunkRepository = chunkRepository;
        this.embeddingClient = embeddingClient;
    }


    public void chunk(Document document) {

        String content = document.getContent();

        int chunkSize = 500;

        List<String> chunks = new ArrayList<>();

        for (int i = 0; i < content.length(); i += chunkSize) {

            int end = Math.min(
                    i + chunkSize,
                    content.length()
            );

            chunks.add(
                    content.substring(i, end)
            );
        }


        int index = 0;

        for(String text : chunks){

            DocumentChunk chunk = new DocumentChunk();

            chunk.setDocument(document);

            chunk.setChunkIndex(index++);

            chunk.setContent(text);

            float[] embedding =
                    embeddingClient.generateEmbedding(text);

            chunk.setEmbedding(embedding);


            System.out.println(
                    "Saving chunk index="
                            + chunk.getChunkIndex()
                            + " embedding size="
                            + embedding.length
            );


            chunkRepository.save(chunk);
        }
    }
}