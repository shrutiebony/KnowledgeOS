package com.knowledgeos.repository;

import com.knowledgeos.model.UrlQueue;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.transaction.annotation.Transactional;

import java.util.Optional;

public interface UrlQueueRepository
        extends JpaRepository<UrlQueue, Long> {


    Optional<UrlQueue> findFirstByVisitedFalse();

    Optional<UrlQueue> findFirstByVisitedFalseAndDatasetKey(String datasetKey);


    boolean existsByUrl(String url);

    boolean existsByUrlAndDatasetKey(String url, String datasetKey);


    long countByVisitedFalse();

    long countByVisitedTrue();

    long countByDatasetKeyAndVisitedFalse(String datasetKey);

    long countByDatasetKeyAndVisitedTrue(String datasetKey);

    @Transactional
    long deleteByVisitedFalse();

    @Transactional
    long deleteByDatasetKeyAndVisitedFalse(String datasetKey);
    @Transactional
    long deleteByDatasetKey(String datasetKey);

}