package com.knowledgeos.service;

import com.knowledgeos.model.GraphEntity;
import com.knowledgeos.repository.*;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;

@Service
public class DatasetService {

    private final DocumentChunkRepository documentChunkRepository;
    private final PageLinkRepository pageLinkRepository;
    private final RelationshipRepository relationshipRepository;
    private final PredictedLinkRepository predictedLinkRepository;
    private final GnnEmbeddingRepository gnnEmbeddingRepository;
    private final GraphEntityRepository graphEntityRepository;
    private final DocumentRepository documentRepository;
    private final UrlQueueRepository urlQueueRepository;

    public DatasetService(
            DocumentChunkRepository documentChunkRepository,
            PageLinkRepository pageLinkRepository,
            RelationshipRepository relationshipRepository,
            PredictedLinkRepository predictedLinkRepository,
            GnnEmbeddingRepository gnnEmbeddingRepository,
            GraphEntityRepository graphEntityRepository,
            DocumentRepository documentRepository,
            UrlQueueRepository urlQueueRepository
    ) {
        this.documentChunkRepository = documentChunkRepository;
        this.pageLinkRepository = pageLinkRepository;
        this.relationshipRepository = relationshipRepository;
        this.predictedLinkRepository = predictedLinkRepository;
        this.gnnEmbeddingRepository = gnnEmbeddingRepository;
        this.graphEntityRepository = graphEntityRepository;
        this.documentRepository = documentRepository;
        this.urlQueueRepository = urlQueueRepository;
    }

    @Transactional
    public void deleteDataset(String datasetKey) {

        // 1. Chunks and page links reference documents - clear first.
        documentChunkRepository.deleteByDocument_DatasetKey(datasetKey);
        pageLinkRepository.deleteBySource_DatasetKey(datasetKey);

        // 2. Relationships reference both documents and entities.
        relationshipRepository.deleteByDocument_DatasetKey(datasetKey);

        // 3. Predicted links and GNN embeddings reference entities.
        predictedLinkRepository.deleteBySource_DatasetKey(datasetKey);

        List<Long> entityIds = graphEntityRepository.findByDatasetKey(datasetKey).stream()
                .map(GraphEntity::getId)
                .toList();

        if (!entityIds.isEmpty()) {
            gnnEmbeddingRepository.deleteByEntityIdIn(entityIds);
        }

        graphEntityRepository.deleteByDatasetKey(datasetKey);

        documentRepository.deleteByDatasetKey(datasetKey);
        urlQueueRepository.deleteByDatasetKey(datasetKey);
    }
}