package com.knowledgeos.controller;

import com.knowledgeos.model.ChatResponse;
import com.knowledgeos.service.ChatService;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/chat")
public class ChatController {


    private final ChatService chatService;


    public ChatController(ChatService chatService){
        this.chatService = chatService;
    }


    @GetMapping
    public ChatResponse chat(
            @RequestParam String query,
            @RequestParam(defaultValue = "default") String datasetKey
    ){

        return chatService.answer(query, datasetKey);

    }
}