package com.knowledgeos.controller;

import com.knowledgeos.model.ClassificationBand;
import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.service.AnalysisService;
import com.knowledgeos.service.GraphSageService;
import com.knowledgeos.service.IngestService;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

@RestController
public class DatasetController {
    private final DatasetRepository datasets;
    private final DocumentRepository documents;
    private final IngestService ingestService;
    private final AnalysisService analysisService;
    private final GraphSageService graphSageService;

    public DatasetController(
            DatasetRepository datasets,
            DocumentRepository documents,
            IngestService ingestService,
            AnalysisService analysisService,
            GraphSageService graphSageService) {
        this.datasets = datasets;
        this.documents = documents;
        this.ingestService = ingestService;
        this.analysisService = analysisService;
        this.graphSageService = graphSageService;
    }

    @GetMapping("/health")
    public Map<String, String> health() {
        return Map.of("status", "ok");
    }

    @GetMapping("/datasets")
    public List<Map<String, Object>> list() {
        List<Dataset> all = datasets.findAllByOrderByCreatedAtDesc();
        List<Dataset> wiki = new ArrayList<>();
        List<Dataset> user = new ArrayList<>();
        for (Dataset dataset : all) {
            switch (dataset.getKind()) {
                case WIKIPEDIA_SAMPLE -> wiki.add(dataset);
                case WIKIPEDIA_SUBSET -> {
                    // Leftover topic clones are not listed. Topics are views on Wikipedia.
                }
                case USER_UPLOAD, USER_URLS -> user.add(dataset);
            }
        }
        List<Dataset> ordered = new ArrayList<>();
        ordered.addAll(wiki);
        Set<String> seenUserNames = new HashSet<>();
        for (Dataset dataset : user) {
            String key = dataset.getName() == null ? "" : dataset.getName().toLowerCase(Locale.ROOT);
            if (!seenUserNames.add(key)) {
                continue;
            }
            ordered.add(dataset);
        }
        return ordered.stream().map(this::brief).toList();
    }

    @PostMapping(path = "/datasets/upload", consumes = MediaType.MULTIPART_FORM_DATA_VALUE)
    public Map<String, Object> upload(
            @RequestParam(value = "name", required = false) String name,
            @RequestParam("files") List<MultipartFile> files) throws IOException {
        Dataset dataset = ingestService.ingestUploads(name, files);
        analysisService.analyze(dataset.getId());
        return brief(datasets.findById(dataset.getId()).orElse(dataset));
    }

    @PostMapping("/datasets/from-urls")
    public Map<String, Object> fromUrls(@RequestBody UrlRequest request) {
        IngestService.UrlIngestResult ingested = ingestService.ingestUrls(request.name(), request.urls());
        analysisService.analyze(ingested.dataset().getId());
        Map<String, Object> out = brief(datasets.findById(ingested.dataset().getId()).orElse(ingested.dataset()));
        out.put("seedCount", ingested.seedCount());
        out.put("extraPages", ingested.extraPages());
        out.put("ingestedPages", ingested.ingestedPages());
        out.put("failedPages", ingested.failedPages());
        out.put("crawlMaxDepth", ingested.maxDepth());
        out.put("crawlMaxPages", ingested.maxPages());
        return out;
    }

    @PostMapping("/datasets/{id}/analyze")
    public Map<String, Object> analyze(@PathVariable Long id) {
        return brief(analysisService.analyze(id));
    }

    @GetMapping("/datasets/{id}/summary")
    public Map<String, Object> summary(
            @PathVariable Long id,
            @RequestParam(defaultValue = "documents") String metric,
            @RequestParam(required = false) String topic,
            @RequestParam(required = false) String ids) {
        return analysisService.summary(id, metric, topic, parseIds(ids));
    }

    @GetMapping("/datasets/{id}/graph")
    public Map<String, Object> graph(
            @PathVariable Long id,
            @RequestParam(required = false) String topic,
            @RequestParam(required = false) String ids) {
        return analysisService.graph(id, topic, parseIds(ids));
    }

    @GetMapping("/datasets/{id}/examples")
    public List<Map<String, Object>> examples(
            @PathVariable Long id,
            @RequestParam ClassificationBand band,
            @RequestParam(defaultValue = "6") int limit,
            @RequestParam(required = false) String topic,
            @RequestParam(required = false) String ids) {
        return analysisService.examples(id, band, limit, topic, parseIds(ids));
    }

    @GetMapping("/datasets/{id}/breakdown")
    public Map<String, Object> breakdown(
            @PathVariable Long id,
            @RequestParam(defaultValue = "source") String by,
            @RequestParam(required = false) String topic,
            @RequestParam(required = false) String ids) {
        List<Map<String, Object>> rows = analysisService.breakdown(id, by, topic, parseIds(ids));
        return Map.of("by", by, "available", !rows.isEmpty(), "rows", rows);
    }

    @GetMapping("/datasets/{id}/documents/{docId}")
    public Map<String, Object> document(@PathVariable Long id, @PathVariable Long docId) {
        return analysisService.explanation(id, docId);
    }

    @GetMapping("/datasets/{id}/topics")
    public List<String> topics(@PathVariable Long id) {
        return ingestService.topics(id);
    }

    @PostMapping("/datasets/{id}/graphsage/train")
    public Map<String, Object> train(@PathVariable Long id, @RequestBody(required = false) Map<String, String> body) {
        String labels = body == null ? null : body.get("labelsPath");
        Dataset dataset = graphSageService.trainClassifier(id, labels);
        return Map.of(
                "status", dataset.getGraphSageStatus(),
                "message", dataset.getGraphSageMessage(),
                "usedInHeadline", false
        );
    }

    private Map<String, Object> brief(Dataset dataset) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("id", dataset.getId());
        m.put("name", dataset.getName());
        m.put("kind", dataset.getKind());
        m.put("analysisState", dataset.getAnalysisState());
        m.put("documentCount", documents.countByDatasetId(dataset.getId()));
        m.put("topics", List.of());
        m.put("graphSageStatus", dataset.getGraphSageStatus());
        m.put("wikipedia", dataset.getKind() == DatasetKind.WIKIPEDIA_SAMPLE);
        return m;
    }

    private static List<Long> parseIds(String ids) {
        if (ids == null || ids.isBlank()) {
            return List.of();
        }
        List<Long> out = new ArrayList<>();
        for (String part : ids.split(",")) {
            String trimmed = part.trim();
            if (trimmed.isEmpty()) {
                continue;
            }
            try {
                out.add(Long.parseLong(trimmed));
            } catch (NumberFormatException ignored) {
                // Ignore malformed tokens; analysis stays on valid ids in this dataset.
            }
        }
        return out;
    }

    public record UrlRequest(String name, List<String> urls) {}
}
