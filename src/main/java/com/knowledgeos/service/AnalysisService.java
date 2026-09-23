package com.knowledgeos.service;

import com.knowledgeos.model.ClassificationBand;
import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentEdge;
import com.knowledgeos.model.DocumentMetaView;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentEdgeRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.Instant;
import java.time.LocalDate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

@Service
public class AnalysisService {
    private final DatasetRepository datasets;
    private final DocumentRepository documents;
    private final DocumentEdgeRepository edges;
    private final EmbeddingService embeddings;
    private final StylometryService stylometry;
    private final DetectorService detectors;
    private final CalibrationService calibration;
    private final GraphSageService graphSage;
    private final int k;
    private final double minCosine;

    public AnalysisService(
            DatasetRepository datasets,
            DocumentRepository documents,
            DocumentEdgeRepository edges,
            EmbeddingService embeddings,
            StylometryService stylometry,
            DetectorService detectors,
            CalibrationService calibration,
            GraphSageService graphSage,
            @Value("${knowledgeos.graph.k:8}") int k,
            @Value("${knowledgeos.graph.min-cosine:0.32}") double minCosine) {
        this.datasets = datasets;
        this.documents = documents;
        this.edges = edges;
        this.embeddings = embeddings;
        this.stylometry = stylometry;
        this.detectors = detectors;
        this.calibration = calibration;
        this.graphSage = graphSage;
        this.k = k;
        this.minCosine = minCosine;
    }

    @Transactional
    public Dataset analyze(Long datasetId) {
        return analyzeInternal(datasetId, true, false);
    }

    /**
     * Score any documents that still lack p(AI). Used while a crawl is still
     * storing pages so header pills are not stuck at zero. GraphSAGE stays off
     * here — it is gated and is not a detection signal.
     */
    @Transactional
    public Dataset analyzeIncremental(Long datasetId) {
        return analyzeInternal(datasetId, false, true);
    }

    public boolean needsAnalysis(Long datasetId) {
        return documents.countByDatasetId(datasetId) > 0
                && documents.countUnscoredByDatasetId(datasetId) > 0;
    }

    @Transactional
    Dataset analyzeInternal(Long datasetId, boolean runGraphSage, boolean incremental) {
        Dataset dataset = datasets.findById(datasetId).orElseThrow();
        List<Document> docs = documents.findByDatasetId(datasetId);
        if (docs.isEmpty()) {
            dataset.setAnalysisState("empty");
            return datasets.save(dataset);
        }
        List<Document> pending = docs.stream()
                .filter(d -> d.getPAi() == null || d.getEmbedding() == null)
                .toList();
        if (incremental && pending.isEmpty()) {
            dataset.setAnalysisState("ready");
            if (dataset.getLastAnalyzedAt() == null) {
                dataset.setLastAnalyzedAt(Instant.now());
            }
            return datasets.save(dataset);
        }

        dataset.setAnalysisState("running");
        datasets.save(dataset);
        assignScores(docs);
        documents.saveAll(docs);
        rebuildGraph(datasetId, docs);
        if (runGraphSage) {
            graphSage.embedUnsupervised(dataset);
        }
        dataset.setLastAnalyzedAt(Instant.now());
        dataset.setAnalysisState("ready");
        return datasets.save(dataset);
    }

    /**
     * Score every document in the collection. Anomaly is collection-relative so a
     * uniform encyclopedia is not swallowed by its own "most human" centroid.
     * Bands mix raw p(AI) with rank. GraphSAGE is not used.
     */
    private void assignScores(List<Document> docs) {
        List<float[]> vectors = new ArrayList<>();
        List<StylometryService.Features> features = new ArrayList<>();
        List<Double> styleAi = new ArrayList<>();
        for (Document doc : docs) {
            StylometryService.Features f = stylometry.analyze(doc.getText());
            features.add(f);
            styleAi.add(stylometry.stylometryAiScore(f));
            doc.setWordCount(f.wordCount());
            doc.setTypeTokenRatio(f.typeTokenRatio());
            doc.setAvgSentenceLength(f.avgSentenceLength());
            doc.setSentenceLengthStd(f.sentenceLengthStd());
            doc.setBurstiness(f.burstiness());
            doc.setPunctuationRatio(f.punctuationRatio());
            doc.setCharEntropy(f.charEntropy());
            doc.setRepetitionScore(f.repetitionScore());
            float[] emb = embeddings.embed(doc.getTitle() + "\n" + doc.getText());
            doc.setEmbedding(emb);
            vectors.add(emb);
        }

        // Milder baseline: lowest-stylometry sixth, not the most-human 40%.
        List<float[]> humanBaseline = new ArrayList<>();
        List<Integer> order = new ArrayList<>();
        for (int i = 0; i < docs.size(); i++) {
            order.add(i);
        }
        order.sort(Comparator.comparingDouble(styleAi::get));
        int baselineN = Math.max(2, Math.min(8, docs.size() / 6));
        for (int i = 0; i < Math.min(baselineN, order.size()); i++) {
            humanBaseline.add(vectors.get(order.get(i)));
        }
        float[] centroid = EmbeddingService.centroid(humanBaseline.isEmpty() ? vectors : humanBaseline);
        double[] ttr = docs.stream().mapToDouble(d -> nz(d.getTypeTokenRatio())).toArray();
        double[] burst = docs.stream().mapToDouble(d -> nz(d.getBurstiness())).toArray();
        Stats ttrStats = stats(ttr);
        Stats burstStats = stats(burst);

        double[] rawAnomaly = new double[docs.size()];
        for (int i = 0; i < docs.size(); i++) {
            rawAnomaly[i] = 1.0 - EmbeddingService.cosine(docs.get(i).getEmbedding(), centroid);
        }
        double[] anomalyRank = ranks01(rawAnomaly);

        List<CalibrationService.Estimate> rawEstimates = new ArrayList<>();
        for (int i = 0; i < docs.size(); i++) {
            Document doc = docs.get(i);
            StylometryService.Features f = features.get(i);
            double anomaly = StylometryService.clamp01(0.45 * StylometryService.clamp01(rawAnomaly[i] * 1.4)
                    + 0.55 * anomalyRank[i]);
            double zTtr = z(f.typeTokenRatio(), ttrStats);
            double zBurst = z(f.burstiness(), burstStats);
            double deviation = StylometryService.clamp01((Math.abs(zTtr) + Math.abs(zBurst)) / 6.0);

            Map<String, Double> raw = new LinkedHashMap<>();
            raw.put("stylometry", stylometry.stylometryAiScore(f));
            for (DetectorService.Detector detector : detectors.detectors()) {
                raw.put(detector.name(), detector.score(doc.getText(), f));
            }
            raw.put("embedding_anomaly", anomaly);
            raw.put("stylometry_deviation", deviation);
            raw.put("post_chatgpt", CalibrationService.postChatgptSignal(doc.getPublishedAt()));

            CalibrationService.Estimate estimate = calibration.calibrate(raw);
            rawEstimates.add(estimate);
            doc.setDetectorStylometry(raw.get("stylometry"));
            doc.setDetectorRepetition(raw.get("ngram_repetition_detector"));
            doc.setDetectorUniformity(raw.get("sentence_uniformity_detector"));
            doc.setEmbeddingAnomaly(anomaly);
            doc.setStylometryDeviation(deviation);
        }

        double[] rawP = rawEstimates.stream().mapToDouble(CalibrationService.Estimate::pAi).toArray();
        double[] pRank = ranks01(rawP);
        for (int i = 0; i < docs.size(); i++) {
            Document doc = docs.get(i);
            CalibrationService.Estimate mixed = calibration.mixWithRank(rawEstimates.get(i), pRank[i]);
            doc.setPAi(mixed.pAi());
            doc.setCiLow(mixed.ciLow());
            doc.setCiHigh(mixed.ciHigh());
            doc.setBand(mixed.band());
            doc.setExplanationJson(toJson(mixed.signals()));
        }
    }

    static double[] ranks01(double[] values) {
        int n = values.length;
        double[] ranks = new double[n];
        if (n == 0) {
            return ranks;
        }
        if (n == 1) {
            ranks[0] = 0.5;
            return ranks;
        }
        Integer[] idx = new Integer[n];
        for (int i = 0; i < n; i++) {
            idx[i] = i;
        }
        Arrays.sort(idx, Comparator.comparingDouble(i -> values[i]));
        for (int start = 0; start < n; ) {
            int end = start;
            while (end + 1 < n && values[idx[end + 1]] == values[idx[start]]) {
                end++;
            }
            double avg = (start + end) / 2.0 / (n - 1.0);
            for (int k = start; k <= end; k++) {
                ranks[idx[k]] = avg;
            }
            start = end + 1;
        }
        return ranks;
    }

    private void rebuildGraph(Long datasetId, List<Document> docs) {
        edges.deleteByDatasetId(datasetId);
        List<DocumentEdge> created = new ArrayList<>();
        for (int i = 0; i < docs.size(); i++) {
            List<Scored> neighbors = new ArrayList<>();
            Document a = docs.get(i);
            for (int j = 0; j < docs.size(); j++) {
                if (i == j) {
                    continue;
                }
                Document b = docs.get(j);
                double cos = EmbeddingService.cosine(a.getEmbedding(), b.getEmbedding());
                boolean topicMatch = a.getTopic() != null && a.getTopic().equals(b.getTopic());
                boolean sourceMatch = a.getSource() != null && a.getSource().equals(b.getSource());
                double score = cos + (topicMatch ? 0.08 : 0) + (sourceMatch ? 0.04 : 0);
                if (cos >= minCosine || topicMatch) {
                    neighbors.add(new Scored(b, score, cos, topicMatch, sourceMatch));
                }
            }
            neighbors.sort(Comparator.comparingDouble((Scored s) -> s.score).reversed());
            int limit = Math.min(k, neighbors.size());
            for (int n = 0; n < limit; n++) {
                Scored s = neighbors.get(n);
                DocumentEdge edge = new DocumentEdge();
                edge.setDatasetId(datasetId);
                edge.setSourceDocumentId(a.getId());
                edge.setTargetDocumentId(s.doc.getId());
                edge.setCosine(s.cos);
                edge.setReason(reason(s));
                created.add(edge);
            }
        }
        edges.saveAll(created);
    }

    private static String reason(Scored s) {
        List<String> parts = new ArrayList<>();
        parts.add("embedding");
        if (s.topic) {
            parts.add("topic");
        }
        if (s.source) {
            parts.add("source");
        }
        return String.join("+", parts);
    }

    public Map<String, Object> summary(Long datasetId, String metric) {
        return summary(datasetId, metric, null, null);
    }

    public Map<String, Object> summary(Long datasetId, String metric, String topic) {
        return summary(datasetId, metric, topic, null);
    }

    public Map<String, Object> summary(Long datasetId, String metric, String topic, List<Long> ids) {
        Dataset dataset = datasets.findById(datasetId).orElseThrow();
        long corpusCount = documents.countByDatasetId(datasetId);
        List<Document> docs = docsForView(datasetId, topic, ids);
        List<Document> scored = docs.stream().filter(d -> d.getPAi() != null).toList();
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("datasetId", dataset.getId());
        out.put("name", dataset.getName());
        out.put("kind", dataset.getKind());
        String viewTopic = emptyToNull(topic);
        out.put("topic", viewTopic);
        out.put("topicView", viewTopic != null);
        out.put("idView", ids != null && !ids.isEmpty());
        out.put("corpusDocumentCount", corpusCount);
        out.put("analysisState", dataset.getAnalysisState());
        out.put("lastAnalyzedAt", dataset.getLastAnalyzedAt());
        out.put("documentCount", docs.size());
        out.put("graphsage", Map.of(
                "status", dataset.getGraphSageStatus(),
                "message", dataset.getGraphSageMessage(),
                "usedInHeadline", false
        ));
        out.put("disclaimer", "Estimates are not proof of authorship. Documents are labeled likely AI-generated, likely human-written, or uncertain.");
        Map<String, Long> bands = new LinkedHashMap<>();
        for (ClassificationBand band : ClassificationBand.values()) {
            if (band == ClassificationBand.PENDING) {
                continue;
            }
            bands.put(band.name(), docs.stream().filter(d -> d.getBand() == band).count());
        }
        out.put("bands", bands);

        if (scored.isEmpty()) {
            out.put("headlineMetric", metric);
            out.put("headline", "Not yet analyzed");
            return out;
        }

        double[] pDocs = scored.stream().mapToDouble(Document::getPAi).toArray();
        Interval docShare = meanInterval(pDocs);
        long words = scored.stream().mapToLong(Document::getWordCount).sum();
        double wordMean = words == 0 ? 0 : scored.stream().mapToDouble(d -> d.getPAi() * d.getWordCount()).sum() / words;
        double wordSe = se(pDocs);
        Interval wordShare = new Interval(wordMean, clamp(wordMean - 1.96 * wordSe), clamp(wordMean + 1.96 * wordSe));
        double docMean = docShare.mean;
        double docLow = docShare.low;
        double docHigh = docShare.high;
        double wordLow = wordShare.low;
        double wordHigh = wordShare.high;
        boolean useWords = "words".equalsIgnoreCase(metric);
        out.put("analyzedWordCount", words);
        out.put("shareOfDocuments", pct(docMean));
        out.put("shareOfDocumentsRange", List.of(pct(docLow), pct(docHigh)));
        out.put("shareOfAnalyzedWords", pct(wordMean));
        out.put("shareOfAnalyzedWordsRange", List.of(pct(wordLow), pct(wordHigh)));
        out.put("headlineMetric", useWords ? "words" : "documents");
        if (useWords) {
            out.put("estimatePercent", pct(wordMean));
            out.put("rangePercent", List.of(pct(wordLow), pct(wordHigh)));
            out.put("headline", "Estimated AI-generated share of analyzed words: " + pct(wordMean) + "%");
        } else {
            out.put("estimatePercent", pct(docMean));
            out.put("rangePercent", List.of(pct(docLow), pct(docHigh)));
            out.put("headline", "Estimated AI-generated share of documents: " + pct(docMean) + "%");
        }
        if (dataset.getKind() == DatasetKind.USER_URLS) {
            datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).ifPresent(wiki -> {
                Map<String, Object> thisCollection = new LinkedHashMap<>();
                thisCollection.put("id", dataset.getId());
                thisCollection.put("name", dataset.getName());
                thisCollection.put("kind", dataset.getKind().name());
                thisCollection.put("documentCount", docs.size());
                thisCollection.put("estimatePercent", out.get("estimatePercent"));
                thisCollection.put("rangePercent", out.get("rangePercent"));
                thisCollection.put("headline", out.get("headline"));
                Map<String, Object> comparison = new LinkedHashMap<>();
                comparison.put("label", "Same analysis methods; two independent collections. Graphs and baselines are not shared.");
                comparison.put("thisCollection", thisCollection);
                comparison.put("wikipediaSample", independentHeadline(wiki, metric));
                out.put("comparison", comparison);
            });
        }
        return out;
    }

    private Map<String, Object> independentHeadline(Dataset other, String metric) {
        List<Document> docs = documents.findMetaByDatasetId(other.getId()).stream()
                .map(AnalysisService::fromMeta)
                .toList();
        List<Document> scored = docs.stream().filter(d -> d.getPAi() != null).toList();
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("id", other.getId());
        m.put("name", other.getName());
        m.put("kind", other.getKind().name());
        m.put("documentCount", docs.size());
        if (scored.isEmpty()) {
            m.put("headline", "Not yet analyzed");
            return m;
        }
        boolean useWords = "words".equalsIgnoreCase(metric);
        if (useWords) {
            long words = scored.stream().mapToLong(Document::getWordCount).sum();
            double wordMean = words == 0 ? 0 : scored.stream().mapToDouble(d -> d.getPAi() * d.getWordCount()).sum() / words;
            m.put("estimatePercent", pct(wordMean));
            m.put("headline", "Estimated AI-generated share of analyzed words: " + pct(wordMean) + "%");
        } else {
            Interval docShare = meanInterval(scored.stream().mapToDouble(Document::getPAi).toArray());
            m.put("estimatePercent", pct(docShare.mean));
            m.put("headline", "Estimated AI-generated share of documents: " + pct(docShare.mean) + "%");
        }
        return m;
    }

    public Map<String, Object> graph(Long datasetId) {
        return graph(datasetId, null, null);
    }

    public Map<String, Object> graph(Long datasetId, String topic) {
        return graph(datasetId, topic, null);
    }

    public Map<String, Object> graph(Long datasetId, String topic, List<Long> ids) {
        List<Document> docs = docsForView(datasetId, topic, ids);
        String groupBy = chooseGraphGroupBy(docs, topic);
        Map<Long, String> groupsById = assignGraphGroups(docs, groupBy);
        List<Map<String, Object>> nodes = new ArrayList<>();
        Set<Long> keep = new HashSet<>();
        Map<String, Integer> groupCounts = new LinkedHashMap<>();
        for (Document doc : docs) {
            keep.add(doc.getId());
            String group = groupsById.getOrDefault(doc.getId(), "Other");
            groupCounts.merge(group, 1, Integer::sum);
            Map<String, Object> n = new LinkedHashMap<>();
            n.put("id", doc.getId());
            n.put("title", doc.getTitle());
            n.put("band", doc.getBand());
            n.put("pAi", doc.getPAi());
            n.put("wordCount", doc.getWordCount());
            n.put("topic", doc.getTopic());
            n.put("source", doc.getSource());
            n.put("group", group);
            nodes.add(n);
        }
        List<Map<String, Object>> groupRows = new ArrayList<>();
        groupCounts.entrySet().stream()
                .sorted(Comparator.<Map.Entry<String, Integer>>comparingInt(Map.Entry::getValue).reversed()
                        .thenComparing(e -> e.getKey().toLowerCase(Locale.ROOT)))
                .forEach(e -> {
                    Map<String, Object> row = new LinkedHashMap<>();
                    row.put("key", e.getKey());
                    row.put("label", e.getKey());
                    row.put("count", e.getValue());
                    groupRows.add(row);
                });
        List<Map<String, Object>> links = new ArrayList<>();
        for (DocumentEdge edge : edges.findByDatasetId(datasetId)) {
            if (!keep.contains(edge.getSourceDocumentId()) || !keep.contains(edge.getTargetDocumentId())) {
                continue;
            }
            Map<String, Object> e = new LinkedHashMap<>();
            e.put("from", edge.getSourceDocumentId());
            e.put("to", edge.getTargetDocumentId());
            e.put("cosine", round(edge.getCosine()));
            e.put("reason", edge.getReason());
            links.add(e);
        }
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("datasetId", datasetId);
        out.put("topic", emptyToNull(topic));
        out.put("idView", ids != null && !ids.isEmpty());
        out.put("groupBy", groupBy);
        out.put("groups", groupRows);
        out.put("nodes", nodes);
        out.put("edges", links);
        return out;
    }

    private static String chooseGraphGroupBy(List<Document> docs, String topicFilter) {
        Set<String> topics = new HashSet<>();
        Set<String> sources = new HashSet<>();
        for (Document doc : docs) {
            String t = emptyToNull(doc.getTopic());
            if (t != null) {
                topics.add(t.toLowerCase(Locale.ROOT));
            }
            String s = emptyToNull(doc.getSource());
            if (s != null) {
                sources.add(s.toLowerCase(Locale.ROOT));
            }
        }
        if (emptyToNull(topicFilter) != null) {
            return sources.size() >= 2 ? "source" : "band";
        }
        if (topics.size() >= 2) {
            return "topic";
        }
        if (sources.size() >= 2) {
            return "source";
        }
        return "band";
    }

    private static Map<Long, String> assignGraphGroups(List<Document> docs, String groupBy) {
        Map<Long, String> raw = new LinkedHashMap<>();
        Map<String, Integer> counts = new LinkedHashMap<>();
        for (Document doc : docs) {
            String label = graphGroupLabel(doc, groupBy);
            raw.put(doc.getId(), label);
            counts.merge(label, 1, Integer::sum);
        }
        if (counts.size() <= 8) {
            return raw;
        }
        List<String> keep = counts.entrySet().stream()
                .sorted(Comparator.<Map.Entry<String, Integer>>comparingInt(Map.Entry::getValue).reversed()
                        .thenComparing(e -> e.getKey().toLowerCase(Locale.ROOT)))
                .limit(7)
                .map(Map.Entry::getKey)
                .toList();
        Set<String> keepSet = new HashSet<>(keep);
        Map<Long, String> folded = new LinkedHashMap<>();
        for (var e : raw.entrySet()) {
            folded.put(e.getKey(), keepSet.contains(e.getValue()) ? e.getValue() : "Other");
        }
        return folded;
    }

    private static String graphGroupLabel(Document doc, String groupBy) {
        if ("topic".equals(groupBy)) {
            String topic = emptyToNull(doc.getTopic());
            return topic != null ? topic : "Uncategorized";
        }
        if ("source".equals(groupBy)) {
            String source = emptyToNull(doc.getSource());
            return source != null ? source : "Unknown source";
        }
        return bandHeading(doc.getBand());
    }

    private static String bandHeading(ClassificationBand band) {
        if (band == ClassificationBand.LIKELY_AI) {
            return "likely AI";
        }
        if (band == ClassificationBand.LIKELY_HUMAN) {
            return "likely human";
        }
        if (band == ClassificationBand.UNCERTAIN) {
            return "uncertain";
        }
        return "pending";
    }

    public List<Map<String, Object>> examples(Long datasetId, ClassificationBand band, int limit) {
        return examples(datasetId, band, limit, null, null);
    }

    public List<Map<String, Object>> examples(Long datasetId, ClassificationBand band, int limit, String topic) {
        return examples(datasetId, band, limit, topic, null);
    }

    public List<Map<String, Object>> examples(Long datasetId, ClassificationBand band, int limit, String topic, List<Long> ids) {
        List<Document> docs = new ArrayList<>(docsForView(datasetId, topic, ids).stream()
                .filter(d -> d.getBand() == band)
                .toList());
        docs.sort(Comparator.comparingDouble((Document d) -> {
            if (band == ClassificationBand.LIKELY_AI) {
                return -nz(d.getPAi());
            }
            if (band == ClassificationBand.LIKELY_HUMAN) {
                return nz(d.getPAi());
            }
            return -interval(d);
        }));
        // Spread across topics rather than only the extreme tail.
        List<Document> spread = spread(docs, limit);
        Map<Long, Document> full = new HashMap<>();
        documents.findAllById(spread.stream().map(Document::getId).toList())
                .forEach(d -> full.put(d.getId(), d));
        List<Map<String, Object>> out = new ArrayList<>();
        for (Document meta : spread) {
            Document doc = full.getOrDefault(meta.getId(), meta);
            out.add(card(doc, false));
        }
        return out;
    }

    public Map<String, Object> explanation(Long datasetId, Long documentId) {
        Document doc = documents.findById(documentId).orElseThrow();
        if (!Objects.equals(doc.getDatasetId(), datasetId)) {
            throw new IllegalArgumentException("Document is not in this dataset");
        }
        Map<String, Object> out = card(doc, true);
        List<Map<String, Object>> neighbors = new ArrayList<>();
        for (DocumentEdge edge : edges.findByDatasetIdAndSourceDocumentId(datasetId, documentId)) {
            documents.findById(edge.getTargetDocumentId()).ifPresent(n -> {
                Map<String, Object> row = card(n, false);
                row.put("cosine", round(edge.getCosine()));
                row.put("reason", edge.getReason());
                neighbors.add(row);
            });
        }
        out.put("neighbors", neighbors);
        out.put("disclaimer", "This is an estimate from calibrated signals, not proof of authorship.");
        return out;
    }

    public List<Map<String, Object>> breakdown(Long datasetId, String by) {
        return breakdown(datasetId, by, null, null);
    }

    public List<Map<String, Object>> breakdown(Long datasetId, String by, String topic) {
        return breakdown(datasetId, by, topic, null);
    }

    public List<Map<String, Object>> breakdown(Long datasetId, String by, String topic, List<Long> ids) {
        List<Document> docs = docsForView(datasetId, topic, ids).stream()
                .filter(d -> d.getPAi() != null)
                .toList();
        Map<String, List<Document>> groups = new HashMap<>();
        for (Document doc : docs) {
            String key = switch (by == null ? "source" : by) {
                case "topic" -> emptyToNull(doc.getTopic());
                case "time" -> year(doc.getPublishedAt());
                default -> emptyToNull(doc.getSource());
            };
            if (key == null) {
                continue;
            }
            groups.computeIfAbsent(key, k -> new ArrayList<>()).add(doc);
        }
        List<Map<String, Object>> rows = new ArrayList<>();
        for (var e : groups.entrySet()) {
            List<Document> g = e.getValue();
            double[] ps = g.stream().mapToDouble(Document::getPAi).toArray();
            Interval iv = meanInterval(ps);
            Map<String, Object> row = new LinkedHashMap<>();
            row.put("key", e.getKey());
            row.put("documentCount", g.size());
            row.put("estimatePercent", pct(iv.mean));
            row.put("rangePercent", List.of(pct(iv.low), pct(iv.high)));
            rows.add(row);
        }
        rows.sort(Comparator.comparing(r -> (String) r.get("key")));
        return rows;
    }

    private Map<String, Object> card(Document doc, boolean includeText) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("id", doc.getId());
        m.put("title", doc.getTitle());
        m.put("url", doc.getUrl());
        m.put("source", doc.getSource());
        m.put("topic", doc.getTopic());
        m.put("publishedAt", doc.getPublishedAt());
        m.put("wordCount", doc.getWordCount());
        m.put("pAi", doc.getPAi() == null ? null : round(doc.getPAi()));
        m.put("ciLow", doc.getCiLow() == null ? null : round(doc.getCiLow()));
        m.put("ciHigh", doc.getCiHigh() == null ? null : round(doc.getCiHigh()));
        m.put("band", doc.getBand());
        m.put("signals", Map.of(
                "stylometry", nz(doc.getDetectorStylometry()),
                "stock_phrase_detector", parseSignal(doc.getExplanationJson(), "stock_phrase_detector"),
                "sentence_uniformity_detector", nz(doc.getDetectorUniformity()),
                "ngram_repetition_detector", nz(doc.getDetectorRepetition()),
                "embedding_anomaly", nz(doc.getEmbeddingAnomaly()),
                "stylometry_deviation", nz(doc.getStylometryDeviation()),
                "post_chatgpt", parseSignal(doc.getExplanationJson(), "post_chatgpt")
        ));
        if (includeText) {
            m.put("text", doc.getText());
        } else {
            String t = doc.getText();
            m.put("excerpt", t == null ? "" : t.substring(0, Math.min(320, t.length())));
        }
        return m;
    }

    private static List<Document> spread(List<Document> docs, int limit) {
        List<Document> out = new ArrayList<>();
        Map<String, Integer> used = new HashMap<>();
        for (Document doc : docs) {
            if (out.size() >= limit) {
                break;
            }
            String topic = doc.getTopic() == null ? "_" : doc.getTopic();
            int n = used.getOrDefault(topic, 0);
            if (n > 1 && out.size() + 1 < limit) {
                continue;
            }
            used.put(topic, n + 1);
            out.add(doc);
        }
        for (Document doc : docs) {
            if (out.size() >= limit) {
                break;
            }
            if (!out.contains(doc)) {
                out.add(doc);
            }
        }
        return out;
    }

    private record Scored(Document doc, double score, double cos, boolean topic, boolean source) {}
    private record Stats(double mean, double std) {}
    private record Interval(double mean, double low, double high) {}

    private static Interval meanInterval(double[] values) {
        if (values.length == 0) {
            return new Interval(0, 0, 0);
        }
        double mean = 0;
        for (double v : values) {
            mean += v;
        }
        mean /= values.length;
        double err = 1.96 * se(values);
        return new Interval(mean, clamp(mean - err), clamp(mean + err));
    }

    private static double se(double[] values) {
        if (values.length < 2) {
            return 0.08;
        }
        double mean = 0;
        for (double v : values) {
            mean += v;
        }
        mean /= values.length;
        double var = 0;
        for (double v : values) {
            var += (v - mean) * (v - mean);
        }
        return Math.sqrt(var / (values.length - 1)) / Math.sqrt(values.length);
    }

    private static double clamp(double v) {
        return StylometryService.clamp01(v);
    }

    private static Stats stats(double[] values) {
        if (values.length == 0) {
            return new Stats(0, 1);
        }
        double mean = 0;
        for (double v : values) {
            mean += v;
        }
        mean /= values.length;
        double var = 0;
        for (double v : values) {
            var += (v - mean) * (v - mean);
        }
        double std = values.length < 2 ? 1 : Math.sqrt(var / (values.length - 1));
        if (std < 1e-6) {
            std = 1;
        }
        return new Stats(mean, std);
    }

    private static double z(double v, Stats s) {
        return (v - s.mean) / s.std;
    }

    private static double nz(Double v) {
        return v == null ? 0 : v;
    }

    private static double interval(Document d) {
        if (d.getCiHigh() == null || d.getCiLow() == null) {
            return 0;
        }
        return d.getCiHigh() - d.getCiLow();
    }

    private static int pct(double p) {
        return (int) Math.round(StylometryService.clamp01(p) * 100);
    }

    private static double round(double v) {
        return Math.round(v * 1000.0) / 1000.0;
    }

    private static String year(LocalDate date) {
        return date == null ? null : String.valueOf(date.getYear());
    }

    private List<Document> docsForView(Long datasetId, String topic) {
        return docsForView(datasetId, topic, null);
    }

    private List<Document> docsForView(Long datasetId, String topic, List<Long> ids) {
        List<Document> docs = documents.findMetaByDatasetId(datasetId).stream()
                .map(AnalysisService::fromMeta)
                .toList();
        String wanted = emptyToNull(topic);
        if (wanted != null) {
            String needle = wanted.trim().toLowerCase(Locale.ROOT);
            docs = docs.stream()
                    .filter(d -> d.getTopic() != null && d.getTopic().trim().toLowerCase(Locale.ROOT).equals(needle))
                    .toList();
        }
        if (ids != null && !ids.isEmpty()) {
            Set<Long> keep = new HashSet<>(ids);
            docs = docs.stream().filter(d -> keep.contains(d.getId())).toList();
        }
        return docs;
    }

    private static Document fromMeta(DocumentMetaView meta) {
        Document doc = new Document();
        doc.setId(meta.id());
        doc.setDatasetId(meta.datasetId());
        doc.setTitle(meta.title() == null ? "Untitled" : meta.title());
        doc.setUrl(meta.url());
        doc.setSource(meta.source());
        doc.setTopic(meta.topic());
        doc.setPublishedAt(meta.publishedAt());
        doc.setWordCount(meta.wordCount());
        doc.setPAi(meta.pAi());
        doc.setCiLow(meta.ciLow());
        doc.setCiHigh(meta.ciHigh());
        doc.setBand(meta.band() == null ? ClassificationBand.PENDING : meta.band());
        doc.setText("");
        return doc;
    }

    private static String emptyToNull(String s) {
        return s == null || s.isBlank() ? null : s.trim();
    }

    private static String toJson(Map<String, Double> signals) {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (var e : signals.entrySet()) {
            if (!first) {
                sb.append(',');
            }
            first = false;
            sb.append('"').append(e.getKey()).append('"').append(':').append(round(e.getValue()));
        }
        sb.append('}');
        return sb.toString();
    }

    private static double parseSignal(String json, String key) {
        if (json == null) {
            return 0;
        }
        String needle = "\"" + key + "\":";
        int i = json.indexOf(needle);
        if (i < 0) {
            return 0;
        }
        int start = i + needle.length();
        int end = start;
        while (end < json.length() && "0123456789.+-eE".indexOf(json.charAt(end)) >= 0) {
            end++;
        }
        try {
            return Double.parseDouble(json.substring(start, end));
        } catch (Exception e) {
            return 0;
        }
    }
}
