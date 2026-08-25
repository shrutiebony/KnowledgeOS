package com.knowledgeos.controller;


import com.knowledgeos.service.ChatService;
import com.knowledgeos.service.LlmService;
import org.springframework.web.bind.annotation.*;
import reactor.core.publisher.Flux;


@RestController
@RequestMapping("/chat")
public class StreamingChatController {


    private final ChatService chatService;

    private final LlmService llmService;



    public StreamingChatController(
            ChatService chatService,
            LlmService llmService
    ){

        this.chatService = chatService;
        this.llmService = llmService;

    }



    @GetMapping(
            value = "/stream",
            produces = "text/event-stream"
    )
    public Flux<String> streamChat(
            @RequestParam String query,
            @RequestParam(defaultValue = "default") String datasetKey
    ){


        String prompt =
                chatService.buildPrompt(query, datasetKey);


        return llmService.generateStream(prompt);

    }

}