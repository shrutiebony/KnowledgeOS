package com.knowledgeos.controller;

import com.knowledgeos.service.LlmService;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/test-llm")
public class TestLlmController {


    private final LlmService llmService;


    public TestLlmController(LlmService llmService){
        this.llmService = llmService;
    }


    @GetMapping
    public String test(){

        return llmService.generate(
                "Explain what a knowledge graph is in one sentence."
        );
    }
}