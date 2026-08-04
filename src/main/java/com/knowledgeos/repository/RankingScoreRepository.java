package com.knowledgeos.repository;

import com.knowledgeos.model.RankingScore;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface RankingScoreRepository extends JpaRepository<RankingScore, Long> {
}