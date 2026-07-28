package com.knowledgeos.service;


import com.knowledgeos.model.DocumentChunk;
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
    ){

        this.repository = repository;

        this.embeddingService = embeddingService;

    }




    public List<DocumentChunk> search(String query){


        float[] embedding =
                embeddingService.generateEmbedding(query);



        return repository.findSimilarChunks(
                embedding
        );

    }


}