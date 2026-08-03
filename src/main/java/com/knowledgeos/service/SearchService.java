package com.knowledgeos.service;

import com.knowledgeos.model.DocumentChunk;
import com.knowledgeos.model.RagSearchResult;
import com.knowledgeos.repository.DocumentChunkRepository;
import org.springframework.stereotype.Service;

import java.time.Instant;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;


@Service
public class SearchService {


    private static final int RRF_K = 60;

    private static final int TOP_K = 5;


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


        List<DocumentChunk> candidates =
                repository.searchSimilar(embedding);


        List<String> queryTerms = tokenize(query);


        List<DocumentChunk> byKeyword = candidates.stream()
                .sorted(Comparator.comparingInt(
                        (DocumentChunk c) -> keywordScore(c, queryTerms)
                ).reversed())
                .toList();


        List<DocumentChunk> byRecency = candidates.stream()
                .sorted(Comparator.comparing(
                        (DocumentChunk c) -> {
                            Instant createdAt = c.getDocument().getCreatedAt();
                            return createdAt == null ? Instant.EPOCH : createdAt;
                        },
                        Comparator.reverseOrder()
                ))
                .toList();


        return candidates.stream()
                .map(chunk -> {

                    int vectorRank = candidates.indexOf(chunk);
                    int keywordRank = byKeyword.indexOf(chunk);
                    int recencyRank = byRecency.indexOf(chunk);

                    double score =
                            rrf(vectorRank) + rrf(keywordRank) + rrf(recencyRank);

                    return new RagSearchResult(
                            chunk.getId(),
                            chunk.getDocument().getId(),
                            chunk.getDocument().getTitle(),
                            chunk.getContent(),
                            score
                    );
                })
                .sorted(Comparator.comparingDouble(RagSearchResult::getScore).reversed())
                .limit(TOP_K)
                .toList();

    }



    private double rrf(int rank){

        return 1.0 / (RRF_K + rank + 1);
    }



    private List<String> tokenize(String text){

        return List.of(
                text.toLowerCase(Locale.ROOT)
                        .split("[^a-z0-9]+")
        );
    }



    private int keywordScore(DocumentChunk chunk, List<String> queryTerms){

        String content = chunk.getContent().toLowerCase(Locale.ROOT);

        int score = 0;

        for(String term : queryTerms){

            if(term.isBlank()){
                continue;
            }

            int index = 0;

            while((index = content.indexOf(term, index)) != -1){
                score++;
                index += term.length();
            }
        }

        return score;
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