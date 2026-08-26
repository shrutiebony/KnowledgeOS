package com.knowledgeos.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.core.ParameterizedTypeReference;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.http.codec.ServerSentEvent;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;
import org.springframework.web.reactive.function.client.WebClient;
import reactor.core.publisher.Flux;

import java.net.http.HttpClient;
import java.time.Duration;
import java.util.List;
import java.util.Map;

/**
 * Calls Anthropic's Messages API. Previously this hit a locally-running
 * Ollama instance (localhost:11434) - that had no path to deployment (no
 * container, no way to run it on Render without a dedicated GPU/CPU box)
 * and meant entity extraction, content analysis, and chat generation only
 * ever worked if you happened to have Ollama installed natively. A hosted
 * API needs one env var (an API key) instead of a whole extra service.
 */
@Service
public class LlmService {

    private static final String API_URL = "https://api.anthropic.com/v1/messages";
    private static final String ANTHROPIC_VERSION = "2023-06-01";

    private final RestClient restClient;
    private final WebClient webClient;
    private final ObjectMapper objectMapper = new ObjectMapper();

    private final String model;
    private final int maxTokens;


    public LlmService(
            @Value("${llm.api.key}") String apiKey,
            @Value("${llm.model:claude-haiku-4-5-20251001}") String model,
            @Value("${llm.max-tokens:1024}") int maxTokens
    ) {

        this.model = model;
        this.maxTokens = maxTokens;

        HttpClient httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .build();

        JdkClientHttpRequestFactory factory = new JdkClientHttpRequestFactory(httpClient);
        factory.setReadTimeout(Duration.ofSeconds(120));

        this.restClient = RestClient.builder()
                .requestFactory(factory)
                .defaultHeader("x-api-key", apiKey)
                .defaultHeader("anthropic-version", ANTHROPIC_VERSION)
                .build();

        this.webClient = WebClient.builder()
                .baseUrl(API_URL)
                .defaultHeader("x-api-key", apiKey)
                .defaultHeader("anthropic-version", ANTHROPIC_VERSION)
                .build();
    }


    public String generate(String prompt) {

        Map<String, Object> request = Map.of(
                "model", model,
                "max_tokens", maxTokens,
                "messages", List.of(Map.of("role", "user", "content", prompt))
        );

        String rawResponse = restClient.post()
                .uri(API_URL)
                .body(request)
                .retrieve()
                .body(String.class);

        try {

            JsonNode root = objectMapper.readTree(rawResponse);
            JsonNode contentArray = root.path("content");

            StringBuilder text = new StringBuilder();
            for (JsonNode block : contentArray) {
                if ("text".equals(block.path("type").asText())) {
                    text.append(block.path("text").asText());
                }
            }

            if (text.isEmpty()) {
                throw new RuntimeException("Empty response from Anthropic API: " + rawResponse);
            }

            return text.toString();

        } catch (Exception e) {
            throw new RuntimeException("Failed to parse Anthropic API response", e);
        }
    }


    /**
     * Streams plain text deltas as they arrive, for StreamingChatController
     * to forward as text/event-stream. Anthropic's stream sends several SSE
     * event types (message_start, content_block_start, content_block_delta,
     * ping, message_delta, message_stop) - only content_block_delta carries
     * actual text, so everything else is filtered out rather than forwarded
     * raw (the previous Ollama-based version forwarded raw NDJSON lines
     * unparsed, which wasn't clean text either - this is a real fix, not
     * just a swap).
     */
    public Flux<String> generateStream(String prompt) {

        Map<String, Object> request = Map.of(
                "model", model,
                "max_tokens", maxTokens,
                "stream", true,
                "messages", List.of(Map.of("role", "user", "content", prompt))
        );

        return webClient.post()
                .bodyValue(request)
                .retrieve()
                .bodyToFlux(new ParameterizedTypeReference<ServerSentEvent<String>>() {})
                .filter(event -> "content_block_delta".equals(event.event()))
                .mapNotNull(event -> {

                    try {

                        JsonNode data = objectMapper.readTree(event.data());
                        JsonNode delta = data.path("delta");

                        if ("text_delta".equals(delta.path("type").asText())) {
                            return delta.path("text").asText();
                        }

                        return null;

                    } catch (Exception e) {
                        return null;
                    }
                });
    }
}