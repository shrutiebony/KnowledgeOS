package com.knowledgeos.model;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;


@Getter
@Setter
@AllArgsConstructor
public class RagSearchResult {


    private Long id;

    private Long documentId;

    private String title;

    private String snippet;


}