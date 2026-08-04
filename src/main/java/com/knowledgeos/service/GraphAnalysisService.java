package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.PageLink;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.PageLinkRepository;
import org.springframework.stereotype.Service;

import java.util.*;

@Service
public class GraphAnalysisService {

    private static final int ITERATIONS = 30;

    private final DocumentRepository documentRepository;
    private final PageLinkRepository pageLinkRepository;

    public GraphAnalysisService(
            DocumentRepository documentRepository,
            PageLinkRepository pageLinkRepository
    ) {
        this.documentRepository = documentRepository;
        this.pageLinkRepository = pageLinkRepository;
    }


    public Map<String, Object> computeHits() {

        List<Document> documents = documentRepository.findAll();

        Map<String, Long> urlToId = new HashMap<>();
        for (Document d : documents) {
            urlToId.put(d.getUrl(), d.getId());
        }

        List<PageLink> links = pageLinkRepository.findAll();

        Map<Long, List<Long>> outLinks = new HashMap<>();
        Map<Long, List<Long>> inLinks = new HashMap<>();

        int edgeCount = 0;

        for (PageLink link : links) {

            Long sourceId = link.getSource().getId();
            Long targetId = urlToId.get(link.getTargetUrl());

            if (targetId == null || sourceId.equals(targetId)) {
                continue;
            }

            outLinks.computeIfAbsent(sourceId, k -> new ArrayList<>()).add(targetId);
            inLinks.computeIfAbsent(targetId, k -> new ArrayList<>()).add(sourceId);

            edgeCount++;
        }

        Map<Long, Double> hub = new HashMap<>();
        Map<Long, Double> authority = new HashMap<>();

        for (Document d : documents) {
            hub.put(d.getId(), 1.0);
            authority.put(d.getId(), 1.0);
        }

        for (int iter = 0; iter < ITERATIONS; iter++) {

            Map<Long, Double> newAuthority = new HashMap<>();
            for (Document d : documents) {
                double sum = 0;
                for (Long inNeighbor : inLinks.getOrDefault(d.getId(), List.of())) {
                    sum += hub.get(inNeighbor);
                }
                newAuthority.put(d.getId(), sum);
            }
            normalize(newAuthority);

            Map<Long, Double> newHub = new HashMap<>();
            for (Document d : documents) {
                double sum = 0;
                for (Long outNeighbor : outLinks.getOrDefault(d.getId(), List.of())) {
                    sum += newAuthority.get(outNeighbor);
                }
                newHub.put(d.getId(), sum);
            }
            normalize(newHub);

            authority = newAuthority;
            hub = newHub;
        }

        for (Document d : documents) {
            d.setAuthorityScore(authority.get(d.getId()));
            d.setHubScore(hub.get(d.getId()));
        }

        documentRepository.saveAll(documents);

        return Map.of(
                "documentsScored", documents.size(),
                "edgesUsed", edgeCount,
                "iterations", ITERATIONS
        );
    }

    private void normalize(Map<Long, Double> scores) {

        double sumOfSquares = 0;
        for (double v : scores.values()) {
            sumOfSquares += v * v;
        }

        double norm = Math.sqrt(sumOfSquares);

        if (norm == 0) {
            return;
        }

        for (Map.Entry<Long, Double> entry : scores.entrySet()) {
            entry.setValue(entry.getValue() / norm);
        }
    }
}
