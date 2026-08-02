package com.knowledgeos.service;

import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;
import org.springframework.web.reactive.function.client.WebClient;
import reactor.core.publisher.Flux;

import java.util.Map;


@Service
public class LlmService {


    private final RestClient restClient;


    private final WebClient webClient;


    public LlmService() {


        SimpleClientHttpRequestFactory factory =
                new SimpleClientHttpRequestFactory();

        factory.setConnectTimeout(30000);
        factory.setReadTimeout(120000);


        this.restClient = RestClient.builder()
                .requestFactory(factory)
                .build();


        this.webClient = WebClient.builder()
                .baseUrl("http://localhost:11434")
                .build();

    }



    public String generate(String prompt) {


        Map<String, Object> request =
                Map.of(
                        "model", "llama3.2:3b",
                        "prompt", prompt,
                        "stream", false,
                        "options", Map.of(
                                "temperature", 0.2
                        )
                );


        Map response =
                restClient.post()
                        .uri("http://localhost:11434/api/generate")
                        .body(request)
                        .retrieve()
                        .body(Map.class);


        if(response == null || response.get("response") == null){

            throw new RuntimeException(
                    "Empty response from Ollama"
            );

        }


        return response.get("response").toString();

    }



    public Flux<String> generateStream(String prompt) {


        Map<String, Object> request =
                Map.of(
                        "model", "llama3.2:3b",
                        "prompt", prompt,
                        "stream", true,
                        "options", Map.of(
                                "temperature", 0.2
                        )
                );


        return webClient.post()
                .uri("/api/generate")
                .bodyValue(request)
                .retrieve()
                .bodyToFlux(String.class);

    }

}