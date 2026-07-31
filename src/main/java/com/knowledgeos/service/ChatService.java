package com.knowledgeos.service;

import com.knowledgeos.model.SearchResult;
import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class ChatService {

    private final SearchService searchService;

    public ChatService(SearchService searchService) {
        this.searchService = searchService;
    }

    public List<SearchResult> answer(String query) {
        return searchService.search(query);
    }
}