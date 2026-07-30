package com.knowledgeos.repository;

import com.knowledgeos.model.Document;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.Optional;

public interface DocumentRepository extends JpaRepository<Document, Long> {

    boolean existsByUrl(String url);

}