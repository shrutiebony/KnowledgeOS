package com.knowledgeos.controller;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.web.bind.annotation.*;

import java.util.Comparator;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/analysis")
@CrossOrigin(origins = "*")
public class ContentAnalysisController {

    private final DocumentRepository documentRepository;

    public ContentAnalysisController(DocumentRepository documentRepository) {
        this.documentRepository = documentRepository;
    }

    @GetMapping("/top")
    public List<Document> top(
            @RequestParam(defaultValue = "aiTextScore") String by,
            @RequestParam(defaultValue = "10") int limit
    ) {
        Comparator<Document> comparator = switch (by) {
            case "typeTokenRatio" -> Comparator.comparing(
                    Document::getTypeTokenRatio, Comparator.nullsLast(Comparator.reverseOrder()));
            case "avgSentenceLength" -> Comparator.comparing(
                    Document::getAvgSentenceLength, Comparator.nullsLast(Comparator.reverseOrder()));
            case "sentenceLengthVariance" -> Comparator.comparing(
                    Document::getSentenceLengthVariance, Comparator.nullsLast(Comparator.reverseOrder()));
            default -> Comparator.comparing(
                    Document::getAiTextScore, Comparator.nullsLast(Comparator.reverseOrder()));
        };

        return documentRepository.findAll().stream()
                .sorted(comparator)
                .limit(limit)
                .toList();
    }

    @GetMapping("/stats")
    public Map<String, Object> stats() {

        List<Document> analyzed = documentRepository.findAll().stream()
                .filter(Document::isContentAnalyzed)
                .filter(d -> d.getAiTextScore() != null)
                .toList();

        double avgScore = analyzed.stream()
                .mapToDouble(Document::getAiTextScore)
                .average()
                .orElse(0);

        long likelyAiCount = analyzed.stream()
                .filter(d -> d.getAiTextScore() > 0.5)
                .count();

        return Map.of(
                "documentsAnalyzed", analyzed.size(),
                "averageAiTextScore", avgScore,
                "likelyAiGenerated", likelyAiCount
        );
    }
}
