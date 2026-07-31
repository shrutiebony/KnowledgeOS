package com.knowledgeos.service;

import com.knowledgeos.model.EmbeddingRequest;
import com.knowledgeos.model.EmbeddingResponse;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.util.List;

@Service
public class EmbeddingService {

    private final RestClient restClient;

    public EmbeddingService() {
        this.restClient = RestClient.create();
    }

    public float[] generateEmbedding(String text) {

        EmbeddingRequest request = new EmbeddingRequest(text);

        EmbeddingResponse response = restClient.post()
                .uri("http://localhost:8000/embed")
                .body(request)
                .retrieve()
                .body(EmbeddingResponse.class);

        assert response != null;
        List<Float> list = response.getEmbedding();

        float[] embedding = new float[list.size()];

        for (int i = 0; i < list.size(); i++) {
            embedding[i] = list.get(i);
        }

        return embedding;
    }
}