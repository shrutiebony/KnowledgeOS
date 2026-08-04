package com.knowledgeos.repository;

import com.knowledgeos.model.UrlQueue;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.transaction.annotation.Transactional;

import java.util.Optional;

public interface UrlQueueRepository
        extends JpaRepository<UrlQueue, Long> {


    Optional<UrlQueue> findFirstByVisitedFalse();


    boolean existsByUrl(String url);


    long countByVisitedFalse();

    long countByVisitedTrue();

    @Transactional
    long deleteByVisitedFalse();

}