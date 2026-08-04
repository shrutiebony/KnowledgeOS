package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.List;

@Component
public class EntityExtractionScheduler {

    private final DocumentRepository documentRepository;
    private final EntityExtractionService extractionService;

    public EntityExtractionScheduler(
            DocumentRepository documentRepository,
            EntityExtractionService extractionService
    ) {
        this.documentRepository = documentRepository;
        this.extractionService = extractionService;
    }

    @Scheduled(initialDelay = 15000, fixedDelay = 20000)
    public void runExtraction() {

        List<Document> pending =
                documentRepository.findTop5ByEntitiesExtractedFalseAndContentIsNotNull();

        System.out.println("EntityExtractionScheduler tick - pending: " + pending.size());

        if (pending.isEmpty()) {
            return;
        }

        System.out.println("Extracting entities for " + pending.size() + " documents");

        for (Document document : pending) {
            extractionService.extract(document);
        }
    }
}