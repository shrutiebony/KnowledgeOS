package com.knowledgeos.model;


import jakarta.persistence.*;
import lombok.Getter;
import lombok.Setter;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;


@Entity
@Table(name = "relationship")
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class Relationship {


    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;



    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "source_entity_id")
    private GraphEntity source;



    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "target_entity_id")
    private GraphEntity target;



    private String relationType;



    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "document_id")
    private Document document;



    public Relationship(
            GraphEntity source,
            GraphEntity target,
            String relationType,
            Document document
    ){

        this.source = source;
        this.target = target;
        this.relationType = relationType;
        this.document = document;

    }

}
