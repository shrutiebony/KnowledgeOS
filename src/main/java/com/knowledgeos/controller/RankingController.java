package com.knowledgeos.controller;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.service.GraphAnalysisService;
import org.springframework.web.bind.annotation.*;

import java.util.Comparator;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/rank")
@CrossOrigin(origins = "*")
public class RankingController {

    private final GraphAnalysisService graphAnalysisService;
    private final DocumentRepository documentRepository;

    public RankingController(
            GraphAnalysisService graphAnalysisService,
            DocumentRepository documentRepository
    ) {
        this.graphAnalysisService = graphAnalysisService;
        this.documentRepository = documentRepository;
    }

    @GetMapping("/compute")
    public Map<String, Object> compute() {
        return graphAnalysisService.computeHits();
    }

    @GetMapping("/top")
    public List<Document> top(
            @RequestParam(defaultValue = "authority") String by,
            @RequestParam(defaultValue = "10") int limit
    ) {
        Comparator<Document> comparator = "hub".equalsIgnoreCase(by)
                ? Comparator.comparing(Document::getHubScore, Comparator.nullsLast(Comparator.reverseOrder()))
                : Comparator.comparing(Document::getAuthorityScore, Comparator.nullsLast(Comparator.reverseOrder()));

        return documentRepository.findAll().stream()
                .sorted(comparator)
                .limit(limit)
                .toList();
    }
}
