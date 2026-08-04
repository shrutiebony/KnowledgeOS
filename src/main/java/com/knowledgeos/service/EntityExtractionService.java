package com.knowledgeos.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.GraphEntity;
import com.knowledgeos.model.Relationship;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.GraphEntityRepository;
import com.knowledgeos.repository.RelationshipRepository;
import org.springframework.stereotype.Service;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Service
public class EntityExtractionService {

    private static final int MAX_CONTENT_CHARS = 3000;

    private final LlmService llmService;
    private final GraphEntityRepository entityRepository;
    private final RelationshipRepository relationshipRepository;
    private final DocumentRepository documentRepository;
    private final ObjectMapper objectMapper = new ObjectMapper();


    public EntityExtractionService(
            LlmService llmService,
            GraphEntityRepository entityRepository,
            RelationshipRepository relationshipRepository,
            DocumentRepository documentRepository
    ) {
        this.llmService = llmService;
        this.entityRepository = entityRepository;
        this.relationshipRepository = relationshipRepository;
        this.documentRepository = documentRepository;
    }


    public void extract(Document document) {

        String content = document.getContent();

        if (content == null || content.isBlank()) {
            document.setEntitiesExtracted(true);
            documentRepository.save(document);
            return;
        }

        String truncated = content.length() > MAX_CONTENT_CHARS
                ? content.substring(0, MAX_CONTENT_CHARS)
                : content;

        String prompt = buildPrompt(truncated);

        try {

            String raw = llmService.generate(prompt);

            String json = stripToJson(raw);

            JsonNode root = objectMapper.readTree(json);

            Map<String, GraphEntity> resolved = new HashMap<>();

            if (root.has("entities")) {
                for (JsonNode e : root.get("entities")) {

                    String name = e.path("name").asText(null);
                    String type = e.path("type").asText("OTHER");

                    if (name == null || name.isBlank()) {
                        continue;
                    }

                    resolved.put(name, resolveEntity(name.trim(), type.trim()));
                }
            }

            if (root.has("relationships")) {
                for (JsonNode r : root.get("relationships")) {

                    String sourceName = r.path("source").asText(null);
                    String targetName = r.path("target").asText(null);
                    String relationType = r.path("type").asText("RELATED_TO");

                    if (sourceName == null || targetName == null) {
                        continue;
                    }

                    GraphEntity source = resolved.computeIfAbsent(
                            sourceName.trim(), n -> resolveEntity(n, "OTHER"));

                    GraphEntity target = resolved.computeIfAbsent(
                            targetName.trim(), n -> resolveEntity(n, "OTHER"));

                    relationshipRepository.save(
                            new Relationship(source, target, relationType.trim(), document)
                    );
                }
            }

            System.out.println(
                    "Extracted " + resolved.size() + " entities from document " + document.getId()
            );

        } catch (Exception e) {

            System.out.println(
                    "GraphEntity extraction failed for document " + document.getId() +
                    ": " + e.getMessage()
            );
        }

        document.setEntitiesExtracted(true);
        documentRepository.save(document);
    }


    private GraphEntity resolveEntity(String name, String type) {

        return entityRepository.findByName(name)
                .map(existing -> {
                    existing.setFrequency(existing.getFrequency() + 1);
                    return entityRepository.save(existing);
                })
                .orElseGet(() -> entityRepository.save(new GraphEntity(name, type)));
    }


    private String buildPrompt(String content) {

        return """
                Extract named entities and relationships from the text below.
                Respond with ONLY raw JSON, no markdown, no explanation, in exactly this shape:

                {
                  "entities": [{"name": "...", "type": "PERSON|ORGANIZATION|TECHNOLOGY|OTHER"}],
                  "relationships": [{"source": "...", "target": "...", "type": "..."}]
                }

                Text:
                """ + content;
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
