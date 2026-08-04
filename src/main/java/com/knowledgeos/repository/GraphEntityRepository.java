package com.knowledgeos.repository;


import com.knowledgeos.model.GraphEntity;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;


public interface GraphEntityRepository
        extends JpaRepository<GraphEntity, Long> {


    Optional<GraphEntity> findByName(String name);

    List<GraphEntity> findTop150ByOrderByFrequencyDesc();

}