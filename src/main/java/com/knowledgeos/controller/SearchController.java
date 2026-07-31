package com.knowledgeos.controller;

import com.knowledgeos.model.SearchResult;
import com.knowledgeos.service.SearchService;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/search")
public class SearchController {

    private final SearchService searchService;

    public SearchController(SearchService searchService) {
        this.searchService = searchService;
    }

    @GetMapping
    public List<SearchResult> search(
            @RequestParam String query
    ) {
        return searchService.search(query);
    }
}