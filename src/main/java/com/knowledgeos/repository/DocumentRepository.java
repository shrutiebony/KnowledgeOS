package com.knowledgeos.repository;

import com.knowledgeos.model.ClassificationBand;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentMetaView;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.time.LocalDate;
import java.util.List;

public interface DocumentRepository extends JpaRepository<Document, Long> {
    List<Document> findByDatasetId(Long datasetId);
    List<Document> findByDatasetIdAndBand(Long datasetId, ClassificationBand band);
    long countByDatasetId(Long datasetId);

    @Query("select count(d) from Document d where d.datasetId = :datasetId and d.pAi is null")
    long countUnscoredByDatasetId(@Param("datasetId") Long datasetId);

    @Query("select count(d) from Document d where d.datasetId = :datasetId and d.publishedAt is not null")
    long countDatedByDatasetId(@Param("datasetId") Long datasetId);

    @Query("""
            select new com.knowledgeos.model.DocumentMetaView(
                d.id, d.datasetId, d.title, d.url, d.source, d.topic, d.publishedAt,
                d.wordCount, d.pAi, d.ciLow, d.ciHigh, d.band)
            from Document d
            where d.datasetId = :datasetId
            """)
    List<DocumentMetaView> findMetaByDatasetId(@Param("datasetId") Long datasetId);

    @Query("""
            select distinct d.topic from Document d
            where d.datasetId = :datasetId and d.topic is not null
            order by d.topic
            """)
    List<String> findDistinctTopicsByDatasetId(@Param("datasetId") Long datasetId);

    @Modifying(clearAutomatically = true, flushAutomatically = true)
    @Query("update Document d set d.publishedAt = :publishedAt where d.id = :id and d.publishedAt is null")
    int updatePublishedAtIfNull(@Param("id") Long id, @Param("publishedAt") LocalDate publishedAt);

    @Modifying(clearAutomatically = true, flushAutomatically = true)
    void deleteByDatasetId(Long datasetId);
}
