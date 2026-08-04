package com.knowledgeos.model;

import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;


@Entity
@Table(name = "graph_entity")
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class GraphEntity {


    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;


    @Column(unique = true, nullable = false)
    private String name;


    private String type;


    private Integer frequency = 1;



    public GraphEntity(
            String name,
            String type
    ){
        this.name = name;
        this.type = type;
        this.frequency = 1;
    }

}