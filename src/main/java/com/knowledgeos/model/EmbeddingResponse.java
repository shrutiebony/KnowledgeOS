package com.knowledgeos.model;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

@Setter
@Getter
public class EmbeddingResponse {

    private List<Float> embedding;


}