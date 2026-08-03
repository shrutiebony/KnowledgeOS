package com.knowledgeos.service;

import com.knowledgeos.model.EmbeddingBatchRequest;
import com.knowledgeos.model.EmbeddingBatchResponse;
import com.knowledgeos.model.EmbeddingRequest;
import com.knowledgeos.model.EmbeddingResponse;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.net.http.HttpClient;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

@Service
public class EmbeddingService {

    private final RestClient restClient;

    public EmbeddingService() {

        HttpClient httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .build();

        JdkClientHttpRequestFactory factory = new JdkClientHttpRequestFactory(httpClient);
        factory.setReadTimeout(Duration.ofSeconds(120));

        this.restClient = RestClient.builder()
                .requestFactory(factory)
                .build();
    }


    private static final int MAX_RETRIES = 3;
    private static final long RETRY_DELAY_MS = 500;


    public float[] generateEmbedding(String text) {

        RuntimeException lastFailure = null;

        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {

            try {
                return callEmbed(text);

            } catch (RuntimeException e) {

                lastFailure = e;

                System.out.println(
                        "Embedding attempt " + attempt + "/" + MAX_RETRIES +
                                " failed: " + e.getMessage()
                );

                if (attempt < MAX_RETRIES) {
                    sleep(RETRY_DELAY_MS * attempt);
                }
            }
        }

        throw lastFailure;
    }


    private float[] callEmbed(String text) {

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


    public List<float[]> generateEmbeddingsBatch(List<String> texts) {

        RuntimeException lastFailure = null;

        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {

            try {
                return callEmbedBatch(texts);

            } catch (RuntimeException e) {

                lastFailure = e;

                System.out.println(
                        "Batch embedding attempt " + attempt + "/" + MAX_RETRIES +
                                " failed for batch of " + texts.size() + ": " + e.getMessage()
                );

                if (attempt < MAX_RETRIES) {
                    sleep(RETRY_DELAY_MS * attempt);
                }
            }
        }

        throw lastFailure;
    }


    private List<float[]> callEmbedBatch(List<String> texts) {

        EmbeddingBatchRequest request = new EmbeddingBatchRequest(texts);

        EmbeddingBatchResponse response = restClient.post()
                .uri("http://localhost:8000/embed_batch")
                .body(request)
                .retrieve()
                .body(EmbeddingBatchResponse.class);

        assert response != null;

        List<List<Float>> vectors = response.getEmbeddings();

        List<float[]> result = new ArrayList<>(vectors.size());

        for (List<Float> vector : vectors) {

            float[] embedding = new float[vector.size()];

            for (int i = 0; i < vector.size(); i++) {
                embedding[i] = vector.get(i);
            }

            result.add(embedding);
        }

        return result;
    }


    private void sleep(long ms) {

        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}