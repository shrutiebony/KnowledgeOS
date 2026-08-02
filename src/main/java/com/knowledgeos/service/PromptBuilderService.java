package com.knowledgeos.service;

import com.knowledgeos.model.RagSearchResult;
import org.springframework.stereotype.Service;
import java.util.List;

@Service
public class PromptBuilderService {


    public String buildPrompt(
            String query,
            List<RagSearchResult> results
    ){

        StringBuilder context = new StringBuilder();


        for(RagSearchResult result : results){

            context.append("Title: ")
                    .append(result.getTitle())
                    .append("\n");

            context.append("Content: ")
                    .append(result.getSnippet())
                    .append("\n\n");
        }


        return """
                You are a helpful knowledge assistant.

                Answer the question using only the provided context.

                Context:
                %s

                Question:
                %s

                Answer:
                """.formatted(
                context,
                query
        );
    }
}