package com.knowledgeos.repository;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;

public interface DatasetRepository extends JpaRepository<Dataset, Long> {
    Optional<Dataset> findFirstByKind(DatasetKind kind);
    Optional<Dataset> findFirstByNameIgnoreCase(String name);
    Optional<Dataset> findFirstByKindAndNameIgnoreCase(DatasetKind kind, String name);
    List<Dataset> findByKindAndNameIgnoreCaseOrderByCreatedAtDesc(DatasetKind kind, String name);
    List<Dataset> findByKindOrderByCreatedAtDesc(DatasetKind kind);
    List<Dataset> findAllByOrderByCreatedAtDesc();
}
