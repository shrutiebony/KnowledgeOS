package com.knowledgeos.repository;

import com.knowledgeos.model.Document;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;

public interface DocumentRepository extends JpaRepository<Document, Long> {

    boolean existsByUrl(String url);

    Optional<Document> findByUrl(String url);

    boolean existsByUrlAndDatasetKey(String url, String datasetKey);

    Optional<Document> findByUrlAndDatasetKey(String url, String datasetKey);

    List<Document> findByDatasetKey(String datasetKey);

    long countByDatasetKey(String datasetKey);

    @org.springframework.data.jpa.repository.Query(
            "SELECT DISTINCT d.datasetKey FROM Document d ORDER BY d.datasetKey")
    List<String> findDistinctDatasetKeys();

    List<Document> findTop5ByEntitiesExtractedFalseAndContentIsNotNull();

    List<Document> findTop5ByContentAnalyzedFalseAndContentIsNotNull();

}