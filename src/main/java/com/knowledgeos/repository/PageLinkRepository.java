package com.knowledgeos.repository;

import com.knowledgeos.model.PageLink;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;

public interface PageLinkRepository extends JpaRepository<PageLink, Long> {

    List<PageLink> findAll();

}