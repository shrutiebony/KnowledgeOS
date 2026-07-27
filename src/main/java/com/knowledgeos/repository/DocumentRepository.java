package com.knowledgeos.repository;

import com.knowledgeos.model.Document;
import org.springframework.data.jpa.repository.JpaRepository;


public interface DocumentRepository extends JpaRepository<Document, Long> {

}