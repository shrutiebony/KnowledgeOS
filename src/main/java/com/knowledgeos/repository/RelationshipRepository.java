package com.knowledgeos.repository;


import com.knowledgeos.model.Relationship;
import org.springframework.data.jpa.repository.JpaRepository;


public interface RelationshipRepository
        extends JpaRepository<Relationship, Long> {

}