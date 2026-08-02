package com.knowledgeos;

import com.knowledgeos.service.EmbeddingMigrationService;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;


@SpringBootApplication
public class Backend {


    public static void main(String[] args) {

        SpringApplication.run(
                Backend.class,
                args
        );

    }



    @Bean
    CommandLineRunner migration(
            EmbeddingMigrationService service
    ){

        return args -> {

            service.generateMissingEmbeddings();

        };

    }

}