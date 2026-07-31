package com.knowledgeos.service;

import org.springframework.stereotype.Service;
import org.springframework.web.reactive.function.client.WebClient;

import java.util.List;
import java.util.Map;

@Service
public class EmbeddingClient {


    private final WebClient webClient;


    public EmbeddingClient(WebClient.Builder builder) {

        this.webClient = builder
                .baseUrl("http://127.0.0.1:8000")
                .build();
    }


    public float[] generateEmbedding(String text) {


        Map response =
                webClient.post()
                        .uri("/embed")
                        .bodyValue(
                                Map.of("text", text)
                        )
                        .retrieve()
                        .bodyToMono(Map.class)
                        .block();


        List<Double> values =
                (List<Double>) response.get("embedding");


        float[] embedding =
                new float[values.size()];


        for(int i = 0; i < values.size(); i++) {

            embedding[i] =
                    values.get(i).floatValue();
        }


        return embedding;
    }
}