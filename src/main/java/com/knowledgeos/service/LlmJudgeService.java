package com.knowledgeos.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.stereotype.Service;

import java.util.Optional;


@Service
public class LlmJudgeService {

    private static final int MAX_CONTENT_CHARS = 3000;

    private final LlmService llmService;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public LlmJudgeService(LlmService llmService) {
        this.llmService = llmService;
    }


    public Optional<Integer> judge(String content, String rubricPrompt) {

        String truncated = content.length() > MAX_CONTENT_CHARS
                ? content.substring(0, MAX_CONTENT_CHARS)
                : content;

        try {

            String raw = llmService.generate(rubricPrompt + "\n\nText:\n" + truncated);

            String json = stripToJson(raw);

            JsonNode root = objectMapper.readTree(json);

            int score = root.path("score").asInt(-1);

            if (score >= 0 && score <= 100) {
                return Optional.of(score);
            }

            return Optional.empty();

        } catch (Exception e) {

            System.out.println("LLM judge call failed: " + e.getMessage());
            return Optional.empty();
        }
    }

    private String stripToJson(String raw) {

        int start = raw.indexOf('{');
        int end = raw.lastIndexOf('}');

        if (start == -1 || end == -1 || end < start) {
            throw new RuntimeException("No JSON object found in LLM response");
        }

        return raw.substring(start, end + 1);
    }
}
