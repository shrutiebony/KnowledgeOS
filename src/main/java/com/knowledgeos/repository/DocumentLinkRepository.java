package com.knowledgeos.repository;

import com.knowledgeos.model.DocumentLink;
import org.springframework.data.jpa.repository.JpaRepository;

public interface DocumentLinkRepository
        extends JpaRepository<DocumentLink, Long> {
}