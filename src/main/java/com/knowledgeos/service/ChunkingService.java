package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.repository.DocumentChunkRepository;

import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;


@Service
public class ChunkingService {


    private final DocumentChunkRepository chunkRepository;


    public ChunkingService(DocumentChunkRepository chunkRepository){

        this.chunkRepository = chunkRepository;

    }



    public void chunk(Document document){


        String content = document.getContent();


        int chunkSize = 500;


        List<String> chunks = new ArrayList<>();


        for(int i = 0; i < content.length(); i += chunkSize){


            int end = Math.min(
                    i + chunkSize,
                    content.length()
            );


            chunks.add(
                    content.substring(i,end)
            );
        }



        int index = 0;


        for(String text : chunks){


            DocumentChunk chunk = new DocumentChunk();


            chunk.setDocument(document);


            chunk.setChunkIndex(index++);


            chunk.setContent(text);



            chunk.setEmbedding(
                    createDummyEmbedding()
            );



            chunkRepository.save(chunk);

        }

    }



    private float[] createDummyEmbedding(){


        float[] vector = new float[1536];


        Random random = new Random();



        for(int i = 0; i < 1536; i++){

            vector[i] = random.nextFloat();

        }


        return vector;

    }

}