package com.knowledgeos.repository;


import com.knowledgeos.model.Relationship;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;


public interface RelationshipRepository
        extends JpaRepository<Relationship, Long> {


    List<Relationship> findBySource_IdInAndTarget_IdIn(
            List<Long> sourceIds,
            List<Long> targetIds
    );

}