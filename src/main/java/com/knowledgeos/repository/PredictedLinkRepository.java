package com.knowledgeos.repository;

import com.knowledgeos.model.PredictedLink;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;

public interface PredictedLinkRepository extends JpaRepository<PredictedLink, Long> {

    List<PredictedLink> findTop50ByOrderByScoreDesc();

    List<PredictedLink> findTop50BySource_DatasetKeyOrderByScoreDesc(String datasetKey);

    void deleteAllInBatch();

    void deleteBySource_DatasetKey(String datasetKey);
}