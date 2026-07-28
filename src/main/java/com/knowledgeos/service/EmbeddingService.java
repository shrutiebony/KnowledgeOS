package com.knowledgeos.service;


import org.springframework.stereotype.Service;

import java.util.Random;


@Service
public class EmbeddingService {


    public float[] generateEmbedding(String text){


        float[] vector = new float[1536];


        Random random = new Random();



        for(int i = 0; i < 1536; i++){

            vector[i] = random.nextFloat();

        }


        return vector;

    }

}