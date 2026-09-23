package com.knowledgeos.service;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

@Service
public class EmbeddingService {
    private final int dim;
    private final String embedUrl;
    private final RestClient restClient = RestClient.create();

    public EmbeddingService(
            @Value("${knowledgeos.embed.dim:128}") int dim,
            @Value("${knowledgeos.embed.url:}") String embedUrl) {
        this.dim = dim;
        this.embedUrl = embedUrl == null ? "" : embedUrl.trim();
    }

    public float[] embed(String text) {
        if (!embedUrl.isBlank()) {
            try {
                Map<String, Object> body = new HashMap<>();
                body.put("texts", List.of(text == null ? "" : text.substring(0, Math.min(text.length(), 8000))));
                Map<?, ?> response = restClient.post()
                        .uri(embedUrl.replaceAll("/$", "") + "/embed_batch")
                        .contentType(MediaType.APPLICATION_JSON)
                        .body(body)
                        .retrieve()
                        .body(Map.class);
                if (response != null && response.get("embeddings") instanceof List<?> embeddings
                        && !embeddings.isEmpty() && embeddings.get(0) instanceof List<?> vector) {
                    float[] out = new float[vector.size()];
                    for (int i = 0; i < vector.size(); i++) {
                        out[i] = ((Number) vector.get(i)).floatValue();
                    }
                    return l2(out);
                }
            } catch (Exception ignored) {
                // Fall back to local hashed embeddings so analysis still runs.
            }
        }
        return hashedEmbed(text);
    }

    public float[] hashedEmbed(String text) {
        float[] vector = new float[dim];
        if (text == null || text.isBlank()) {
            return vector;
        }
        String[] tokens = text.toLowerCase(Locale.ROOT).split("[^a-z0-9]+");
        for (String token : tokens) {
            if (token.length() < 2) {
                continue;
            }
            int h = Math.abs(token.hashCode());
            vector[h % dim] += 1.0f;
            int h2 = Math.abs((token + "#2").hashCode());
            vector[h2 % dim] += 0.5f;
        }
        byte[] extra = text.getBytes(StandardCharsets.UTF_8);
        for (int i = 0; i < extra.length; i += 7) {
            vector[Math.floorMod(extra[i], dim)] += 0.05f;
        }
        return l2(vector);
    }

    public static double cosine(float[] a, float[] b) {
        if (a == null || b == null) {
            return 0;
        }
        int n = Math.min(a.length, b.length);
        double dot = 0;
        double na = 0;
        double nb = 0;
        for (int i = 0; i < n; i++) {
            dot += a[i] * b[i];
            na += a[i] * a[i];
            nb += b[i] * b[i];
        }
        if (na == 0 || nb == 0) {
            return 0;
        }
        return dot / (Math.sqrt(na) * Math.sqrt(nb));
    }

    public static float[] centroid(List<float[]> vectors) {
        if (vectors == null || vectors.isEmpty() || vectors.get(0) == null) {
            return new float[0];
        }
        int n = vectors.get(0).length;
        float[] c = new float[n];
        int count = 0;
        for (float[] v : vectors) {
            if (v == null || v.length != n) {
                continue;
            }
            count++;
            for (int i = 0; i < n; i++) {
                c[i] += v[i];
            }
        }
        if (count == 0) {
            return c;
        }
        for (int i = 0; i < n; i++) {
            c[i] /= count;
        }
        return l2(c);
    }

    private static float[] l2(float[] vector) {
        double n = 0;
        for (float v : vector) {
            n += v * v;
        }
        if (n == 0) {
            return vector;
        }
        float inv = (float) (1.0 / Math.sqrt(n));
        for (int i = 0; i < vector.length; i++) {
            vector[i] *= inv;
        }
        return vector;
    }
}
