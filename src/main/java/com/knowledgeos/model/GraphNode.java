package com.knowledgeos.model;

public class GraphNode {

    private Long id;
    private String name;
    private String type;
    private Integer frequency;

    public GraphNode(Long id, String name, String type, Integer frequency) {
        this.id = id;
        this.name = name;
        this.type = type;
        this.frequency = frequency;
    }

    public Long getId() { return id; }
    public String getName() { return name; }
    public String getType() { return type; }
    public Integer getFrequency() { return frequency; }
}