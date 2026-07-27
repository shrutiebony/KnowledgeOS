package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

@Service
public class ChunkingService {


    private final DocumentChunkRepository chunkRepository;


    public ChunkingService(DocumentChunkRepository chunkRepository) {
        this.chunkRepository = chunkRepository;
    }


    public void chunk(Document document){

        String text = document.getContent();


        int chunkSize = 500;


        for(int i=0; i<text.length(); i+=chunkSize){

            int end = Math.min(
                    i + chunkSize,
                    text.length()
            );


            DocumentChunk chunk = new DocumentChunk();

            chunk.setDocument(document);

            chunk.setContent(
                    text.substring(i,end)
            );


            chunkRepository.save(chunk);
        }
    }
}