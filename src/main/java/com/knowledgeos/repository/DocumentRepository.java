package com.knowledgeos.repository;

import com.knowledgeos.model.Document;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.List;
import java.util.Optional;

public interface DocumentRepository extends JpaRepository<Document, Long> {


    Optional<Document> findByUrl(String url);


    boolean existsByUrl(String url);


    @Query("""
        SELECT d
        FROM Document d
        WHERE LOWER(d.title) LIKE LOWER(CONCAT('%', :query, '%'))
        OR LOWER(d.content) LIKE LOWER(CONCAT('%', :query, '%'))
    """)
    List<Document> search(
            @Param("query") String query
    );

}