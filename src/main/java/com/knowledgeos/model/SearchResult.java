package com.knowledgeos.model;

import lombok.AllArgsConstructor;
import lombok.Getter;


@Getter
@AllArgsConstructor
public class SearchResult {

    private Long id;

    private String title;

    private String url;

    private double score;
}