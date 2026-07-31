package com.knowledgeos.controller;

import com.knowledgeos.model.SearchResult;
import com.knowledgeos.service.ChatService;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/chat")
public class ChatController {

    private final ChatService chatService;

    public ChatController(ChatService chatService) {
        this.chatService = chatService;
    }

    @GetMapping
    public List<SearchResult> chat(
            @RequestParam String query
    ) {
        return chatService.answer(query);
    }
}