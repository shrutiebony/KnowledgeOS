package com.knowledgeos.service;

import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Component
public class EmbeddingMigrationScheduler {

    private final EmbeddingMigrationService migrationService;

    public EmbeddingMigrationScheduler(EmbeddingMigrationService migrationService) {
        this.migrationService = migrationService;
    }

    @Scheduled(initialDelay = 10000, fixedDelay = 60000)
    public void runMigration() {
        migrationService.generateMissingEmbeddings();
    }
}