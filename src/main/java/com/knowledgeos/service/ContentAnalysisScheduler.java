package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.List;

@Component
public class ContentAnalysisScheduler {

    private final DocumentRepository documentRepository;
    private final AiTextDetectionService aiTextDetectionService;

    public ContentAnalysisScheduler(
            DocumentRepository documentRepository,
            AiTextDetectionService aiTextDetectionService
    ) {
        this.documentRepository = documentRepository;
        this.aiTextDetectionService = aiTextDetectionService;
    }

    @Scheduled(initialDelay = 25000, fixedDelay = 20000)
    public void runAnalysis() {

        List<Document> pending =
                documentRepository.findTop5ByContentAnalyzedFalseAndContentIsNotNull();

        System.out.println("ContentAnalysisScheduler tick - pending: " + pending.size());

        if (pending.isEmpty()) {
            return;
        }

        System.out.println("Analyzing content for " + pending.size() + " documents");

        for (Document document : pending) {
            aiTextDetectionService.analyze(document);
        }
    }
}