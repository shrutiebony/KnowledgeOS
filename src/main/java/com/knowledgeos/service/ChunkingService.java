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


    public ChunkingService(DocumentChunkRepository repository){
        this.repository = repository;
    }



    public void chunk(Document document){


        String content = document.getContent();


        int chunkSize = 500;


        List<DocumentChunk> chunks = new ArrayList<>();


        for(int i = 0; i < content.length(); i += chunkSize){


            int end = Math.min(
                    i + chunkSize,
                    content.length()
            );


            String text =
                    content.substring(i,end);



            DocumentChunk chunk =
                    new DocumentChunk();


            chunk.setDocument(document);
            chunk.setContent(text);


            chunks.add(chunk);

        }


        repository.saveAll(chunks);


        System.out.println(
                "Created chunks: " + chunks.size()
        );
    }
}