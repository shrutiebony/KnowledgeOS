package com.knowledgeos.service;

import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.model.RagSearchResult;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import javax.naming.directory.SearchResult;
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



    public List<RagSearchResult> search(String query){


        float[] vector =
                embeddingService.generateEmbedding(query);


        String embedding =
                convert(vector);



        List<DocumentChunk> chunks =
                repository.searchSimilar(embedding);



        return chunks.stream()
                .map(chunk ->
                        new RagSearchResult(
                                chunk.getId(),
                                chunk.getDocument().getId(),
                                chunk.getDocument().getTitle(),
                                chunk.getContent()
                        )
                )
                .toList();

    }



    private String convert(float[] vector){

        StringBuilder sb =
                new StringBuilder("[");


        for(int i = 0; i < vector.length; i++){

            sb.append(vector[i]);

            if(i < vector.length - 1){
                sb.append(",");
            }
        }


        sb.append("]");


        return sb.toString();
    }

}