package com.knowledgeos.controller;

import com.knowledgeos.model.GnnEmbedding;
import com.knowledgeos.model.GraphEntity;
import com.knowledgeos.model.PredictedLink;
import com.knowledgeos.model.gnn.PredictedLinkView;
import com.knowledgeos.model.gnn.SimilarEntityView;
import com.knowledgeos.repository.GraphEntityRepository;
import com.knowledgeos.repository.PredictedLinkRepository;
import com.knowledgeos.service.GnnService;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/gnn")
@CrossOrigin(origins = "*")
public class GnnController {

    private final GnnService gnnService;
    private final GraphEntityRepository entityRepository;
    private final PredictedLinkRepository predictedLinkRepository;

    public GnnController(
            GnnService gnnService,
            GraphEntityRepository entityRepository,
            PredictedLinkRepository predictedLinkRepository
    ) {
        this.gnnService = gnnService;
        this.entityRepository = entityRepository;
        this.predictedLinkRepository = predictedLinkRepository;
    }

    @PostMapping("/train")
    public Map<String, Object> train(
            @RequestParam(defaultValue = "50") int topKPredictedLinks,
            @RequestParam(defaultValue = "default") String datasetKey
    ) {
        return gnnService.trainAndRefresh(topKPredictedLinks, datasetKey);
    }

    @GetMapping("/similar/{entityId}")
    public List<SimilarEntityView> similar(
            @PathVariable Long entityId,
            @RequestParam(defaultValue = "10") int limit
    ) {
        List<GnnEmbedding> neighbors = gnnService.findSimilarEntities(entityId, limit);

        return neighbors.stream()
                .map(n -> entityRepository.findById(n.getEntityId()).orElse(null))
                .filter(java.util.Objects::nonNull)
                .map(e -> new SimilarEntityView(e.getId(), e.getName(), e.getType()))
                .toList();
    }

    @GetMapping("/predicted-links")
    public List<PredictedLinkView> predictedLinks(
            @RequestParam(defaultValue = "50") int limit,
            @RequestParam(defaultValue = "default") String datasetKey
    ) {
        List<PredictedLink> links =
                predictedLinkRepository.findTop50BySource_DatasetKeyOrderByScoreDesc(datasetKey);

        return links.stream()
                .limit(limit)
                .map(l -> new PredictedLinkView(
                        l.getSource().getId(),
                        l.getSource().getName(),
                        l.getTarget().getId(),
                        l.getTarget().getName(),
                        l.getScore()
                ))
                .toList();
    }
}