package com.knowledgeos;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;


@SpringBootApplication
@EnableScheduling
public class Backend {


    public static void main(String[] args) {

        SpringApplication.run(
                Backend.class,
                args
        );

    }

}