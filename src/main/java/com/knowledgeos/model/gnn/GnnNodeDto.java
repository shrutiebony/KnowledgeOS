package com.knowledgeos.model.gnn;

public class GnnNodeDto {

    private Long id;
    private String type;
    private Integer frequency;

    public GnnNodeDto(Long id, String type, Integer frequency) {
        this.id = id;
        this.type = type;
        this.frequency = frequency;
    }

    public Long getId() { return id; }
    public String getType() { return type; }
    public Integer getFrequency() { return frequency; }
}