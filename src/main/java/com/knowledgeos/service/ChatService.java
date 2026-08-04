package com.knowledgeos.service;

import com.knowledgeos.model.ChatResponse;
import com.knowledgeos.model.RagSearchResult;
import org.springframework.stereotype.Service;

import java.util.List;


@Service
public class ChatService {


    private final SearchService searchService;
    private final LlmService llmService;


    public ChatService(
            SearchService searchService,
            LlmService llmService
    ){
        this.searchService = searchService;
        this.llmService = llmService;
    }



    public ChatResponse answer(String query){


        List<RagSearchResult> results =
                searchService.search(query);



        StringBuilder context = new StringBuilder();



        int index = 1;

        for(RagSearchResult result : results){

            context.append("[Document ")
                    .append(index++)
                    .append("]\n");

            context.append("Title: ")
                    .append(result.getTitle())
                    .append("\n");

            context.append("Content: ")
                    .append(result.getSnippet())
                    .append("\n\n");
        }



        String prompt =
                """
                SYSTEM:
                You are KnowledgeOS AI assistant.
                Answer only using the provided context.
                If the answer is not present in the context, say:
                "I don't have enough information."

                CONTEXT:

                %s

                QUESTION:
                %s

                ANSWER:
                """.formatted(
                        context,
                        query
                );



        String answer =
                llmService.generate(prompt);



        List<String> sources =
                results.stream()
                        .map(RagSearchResult::getTitle)
                        .distinct()
                        .toList();



        return new ChatResponse(
                answer,
                sources
        );

    }
    public String buildPrompt(String query){


        List<RagSearchResult> results =
                searchService.search(query);


        StringBuilder context = new StringBuilder();


        int index = 1;


        for(RagSearchResult result : results){

            context.append("[Document ")
                    .append(index++)
                    .append("]\n");

            context.append("Title: ")
                    .append(result.getTitle())
                    .append("\n");

            context.append("Content: ")
                    .append(result.getSnippet())
                    .append("\n\n");
        }


        return """
            SYSTEM:
            You are KnowledgeOS AI assistant.
            Answer only using the provided context.

            CONTEXT:

            %s

            QUESTION:
            %s

            ANSWER:
            """.formatted(
                context,
                query
        );
    }

}