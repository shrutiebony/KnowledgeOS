package com.knowledgeos.repository;


import com.knowledgeos.model.KnowledgeEntity;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.Optional;


public interface EntityRepository
        extends JpaRepository<KnowledgeEntity, Long> {


    Optional<KnowledgeEntity> findByName(String name);

}