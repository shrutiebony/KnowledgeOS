package com.knowledgeos.repository;


import com.knowledgeos.model.GraphEntity;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;


public interface GraphEntityRepository
        extends JpaRepository<GraphEntity, Long> {


    Optional<GraphEntity> findByName(String name);

    Optional<GraphEntity> findByNameAndDatasetKey(String name, String datasetKey);

    List<GraphEntity> findTop150ByOrderByFrequencyDesc();

    List<GraphEntity> findTop150ByDatasetKeyOrderByFrequencyDesc(String datasetKey);

    List<GraphEntity> findByDatasetKey(String datasetKey);

    void deleteByDatasetKey(String datasetKey);

}