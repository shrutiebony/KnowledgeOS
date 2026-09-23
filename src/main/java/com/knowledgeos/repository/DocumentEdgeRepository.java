package com.knowledgeos.repository;

import com.knowledgeos.model.DocumentEdge;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;

import java.util.List;

public interface DocumentEdgeRepository extends JpaRepository<DocumentEdge, Long> {
    List<DocumentEdge> findByDatasetId(Long datasetId);
    List<DocumentEdge> findByDatasetIdAndSourceDocumentId(Long datasetId, Long sourceDocumentId);

    @Modifying(clearAutomatically = true, flushAutomatically = true)
    void deleteByDatasetId(Long datasetId);
}
