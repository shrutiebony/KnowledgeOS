package com.knowledgeos;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableAsync;

@SpringBootApplication
@EnableAsync
public class KnowledgeOsApplication {
    public static void main(String[] args) {
        SpringApplication.run(KnowledgeOsApplication.class, args);
    }
}
