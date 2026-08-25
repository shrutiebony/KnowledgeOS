package com.knowledgeos.controller;

import com.knowledgeos.service.WikipediaIngestionService;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/datasets/wikipedia")
@CrossOrigin(origins = "*")
public class WikipediaController {

    private final WikipediaIngestionService wikipediaIngestionService;

    public WikipediaController(WikipediaIngestionService wikipediaIngestionService) {
        this.wikipediaIngestionService = wikipediaIngestionService;
    }

    @PostMapping("/build")
    public Map<String, Object> build(
            @RequestParam List<String> categories,
            @RequestParam(defaultValue = "2") int maxDepth,
            @RequestParam(defaultValue = "300") int maxPages,
            @RequestParam(defaultValue = "wikipedia-snapshot") String datasetKey
    ) {
        return wikipediaIngestionService.buildSnapshot(categories, maxDepth, maxPages, datasetKey);
    }
}