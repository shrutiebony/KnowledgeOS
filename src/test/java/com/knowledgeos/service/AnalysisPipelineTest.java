package com.knowledgeos.service;

import com.knowledgeos.model.ClassificationBand;
import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.TestPropertySource;

import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

@SpringBootTest
@TestPropertySource(properties = {
        "spring.datasource.url=jdbc:h2:mem:kos-test;DB_CLOSE_DELAY=-1",
        "knowledgeos.seed.wikipedia=true",
        "knowledgeos.wikipedia-india.crawl=false"
})
class AnalysisPipelineTest {
    @Autowired
    DatasetRepository datasets;
    @Autowired
    DocumentRepository documents;
    @Autowired
    AnalysisService analysisService;
    @Autowired
    IngestService ingestService;

    @Test
    void wikipediaSampleIsAnalyzedIndependently() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        assertTrue(documents.countByDatasetId(wiki.getId()) > 20);
        var summary = analysisService.summary(wiki.getId(), "documents");
        assertNotNull(summary.get("headline"));
        assertTrue(String.valueOf(summary.get("headline")).contains("documents"));
        assertFalse((Boolean) ((java.util.Map<?, ?>) summary.get("graphsage")).get("usedInHeadline"));
        assertTrue(documents.findByDatasetId(wiki.getId()).stream().anyMatch(d -> d.getBand() != ClassificationBand.PENDING));
        @SuppressWarnings("unchecked")
        var bands = (Map<String, Number>) summary.get("bands");
        assertNotNull(bands);
        long bandTotal = bands.values().stream().mapToLong(Number::longValue).sum();
        assertTrue(bandTotal > 0);
        assertTrue((Number) summary.get("documentCount") instanceof Number);
        assertTrue(((Number) summary.get("documentCount")).intValue() > 20);
        var time = analysisService.breakdown(wiki.getId(), "time");
        assertFalse(time.isEmpty());
        assertTrue(time.stream().anyMatch(r -> String.valueOf(r.get("key")).matches("\\d{4}")));
        var graph = analysisService.graph(wiki.getId());
        @SuppressWarnings("unchecked")
        var nodes = (List<Map<String, Object>>) graph.get("nodes");
        assertEquals(((Number) summary.get("documentCount")).intValue(), nodes.size());
        assertTrue(nodes.stream().anyMatch(n -> n.get("group") instanceof String && !((String) n.get("group")).isBlank()));
        assertTrue(((Number) bands.getOrDefault("LIKELY_AI", 0)).longValue() > 0);
        assertTrue(((Number) bands.getOrDefault("LIKELY_HUMAN", 0)).longValue() > 0);
        assertTrue(((Number) bands.getOrDefault("UNCERTAIN", 0)).longValue() > 0);
        assertNotNull(summary.get("shareOfDocuments"));
        assertNotNull(summary.get("shareOfAnalyzedWords"));
        double later = time.stream()
                .filter(r -> Integer.parseInt(String.valueOf(r.get("key"))) >= 2023)
                .mapToInt(r -> ((Number) r.get("estimatePercent")).intValue())
                .average()
                .orElse(0);
        double earlier = time.stream()
                .filter(r -> Integer.parseInt(String.valueOf(r.get("key"))) <= 2022)
                .mapToInt(r -> ((Number) r.get("estimatePercent")).intValue())
                .average()
                .orElse(later);
        assertTrue(later >= earlier);
    }

    @Test
    void wikipediaTopicIsAViewOnTheSameDataset() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        long datasetsBefore = datasets.count();
        List<String> topics = ingestService.topics(wiki.getId());
        assertFalse(topics.isEmpty());
        String topic = topics.get(0);
        var full = analysisService.summary(wiki.getId(), "documents");
        var view = analysisService.summary(wiki.getId(), "documents", topic);
        assertEquals(wiki.getId(), view.get("datasetId"));
        assertEquals(datasetsBefore, datasets.count());
        assertTrue((Boolean) view.get("topicView"));
        assertEquals(topic, view.get("topic"));
        assertFalse((Boolean) ((Map<?, ?>) view.get("graphsage")).get("usedInHeadline"));
        int viewCount = ((Number) view.get("documentCount")).intValue();
        int fullCount = ((Number) full.get("documentCount")).intValue();
        assertTrue(viewCount > 0);
        assertTrue(viewCount < fullCount);
        assertEquals(fullCount, ((Number) view.get("corpusDocumentCount")).intValue());
        assertTrue(datasets.findAll().stream().noneMatch(d -> d.getKind() == DatasetKind.WIKIPEDIA_SUBSET));
        var graph = analysisService.graph(wiki.getId(), topic);
        @SuppressWarnings("unchecked")
        var nodes = (List<Map<String, Object>>) graph.get("nodes");
        assertEquals(viewCount, nodes.size());
        assertTrue(nodes.stream().allMatch(n -> topic.equalsIgnoreCase(String.valueOf(n.get("topic")))));
        assertEquals(wiki.getId(), graph.get("datasetId"));
        assertTrue(List.of("source", "band").contains(graph.get("groupBy")));
        assertTrue(nodes.stream().allMatch(n -> n.get("group") instanceof String && !((String) n.get("group")).isBlank()));
        var fullGraph = analysisService.graph(wiki.getId());
        assertEquals("topic", fullGraph.get("groupBy"));
    }

    @Test
    void visibleIdsScopeSummaryAndExamplesWithoutNewDataset() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        long datasetsBefore = datasets.count();
        List<Document> docs = documents.findByDatasetId(wiki.getId());
        assertTrue(docs.size() >= 3);
        List<Long> ids = docs.stream().limit(3).map(Document::getId).toList();
        var summary = analysisService.summary(wiki.getId(), "documents", null, ids);
        assertEquals(wiki.getId(), summary.get("datasetId"));
        assertEquals(datasetsBefore, datasets.count());
        assertEquals(Boolean.TRUE, summary.get("idView"));
        assertEquals(3, ((Number) summary.get("documentCount")).intValue());
        assertEquals(docs.size(), ((Number) summary.get("corpusDocumentCount")).intValue());
        assertFalse((Boolean) ((Map<?, ?>) summary.get("graphsage")).get("usedInHeadline"));
        var graph = analysisService.graph(wiki.getId(), null, ids);
        @SuppressWarnings("unchecked")
        var nodes = (List<Map<String, Object>>) graph.get("nodes");
        assertEquals(3, nodes.size());
        assertTrue(nodes.stream().allMatch(n -> ids.contains(((Number) n.get("id")).longValue())));
        var examples = analysisService.examples(wiki.getId(), ClassificationBand.LIKELY_AI, 6, null, ids);
        assertTrue(examples.stream().allMatch(e -> ids.contains(((Number) e.get("id")).longValue())));
    }

    @Test
    void computerScienceTopicNeverCreatesADataset() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        long datasetsBefore = datasets.count();
        var view = analysisService.summary(wiki.getId(), "documents", "Computer Science");
        assertEquals(wiki.getId(), view.get("datasetId"));
        assertEquals(datasetsBefore, datasets.count());
        assertTrue(datasets.findAll().stream().noneMatch(d ->
                d.getName() != null && d.getName().toLowerCase().contains("computer science")));
        assertTrue(datasets.findAll().stream().noneMatch(d -> d.getKind() == DatasetKind.WIKIPEDIA_SUBSET));
    }

    @Test
    void incrementalAnalyzeScoresNewDocumentsWithoutMixing() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        long wikiCount = documents.countByDatasetId(wiki.getId());
        Dataset extra = ingestService.createDataset("Incremental slice", DatasetKind.USER_UPLOAD);
        ingestService.addDocument(extra, "A", "The porch light flickered whenever the wind pushed the door.", null, "notes", "general", java.time.LocalDate.of(2020, 1, 1));
        ingestService.addDocument(extra, "B", "It is important to note that this article provides a comprehensive overview in today's world.", null, "notes", "general", java.time.LocalDate.of(2021, 6, 1));
        analysisService.analyzeIncremental(extra.getId());
        var summary = analysisService.summary(extra.getId(), "documents");
        @SuppressWarnings("unchecked")
        var bands = (Map<String, Number>) summary.get("bands");
        assertTrue(bands.values().stream().mapToLong(Number::longValue).sum() >= 2);
        ingestService.addDocument(extra, "C", "Rain sat in the ruts and the dog refused the short walk to the gate.", null, "notes", "general", java.time.LocalDate.of(2022, 3, 1));
        analysisService.analyzeIncremental(extra.getId());
        var again = analysisService.summary(extra.getId(), "documents");
        assertEquals(3, ((Number) again.get("documentCount")).intValue());
        @SuppressWarnings("unchecked")
        var bands2 = (Map<String, Number>) again.get("bands");
        assertEquals(3, bands2.values().stream().mapToLong(Number::longValue).sum());
        var time = analysisService.breakdown(extra.getId(), "time");
        assertEquals(3, time.size());
        assertEquals(wikiCount, documents.countByDatasetId(wiki.getId()));
    }

    @Test
    void userDatasetDoesNotMixWithWikipedia() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        Dataset user = ingestService.createDataset("Medical demo", DatasetKind.USER_UPLOAD);
        ingestService.addDocument(user, "Note", "It is important to note that vaccines play a crucial role in today's world. This article provides a comprehensive overview of vaccines. In conclusion, vaccines are essential.", null, "clinic", "medicine", null);
        ingestService.addDocument(user, "Clinic memo", "We delayed the second visit because the peak-flow numbers were all over the place and the inhaler technique was still wrong.", null, "clinic", "medicine", null);
        analysisService.analyze(user.getId());
        long wikiCount = documents.countByDatasetId(wiki.getId());
        long userCount = documents.countByDatasetId(user.getId());
        assertTrue(wikiCount > userCount);
        assertTrue(userCount >= 2);
        var graph = analysisService.graph(user.getId());
        @SuppressWarnings("unchecked")
        var nodes = (java.util.List<?>) graph.get("nodes");
        assertTrue(nodes.size() == userCount);
    }

    @Test
    void urlCollectionStaysIndependentAndComparesToWikipedia() {
        Dataset wiki = datasets.findFirstByKind(DatasetKind.WIKIPEDIA_SAMPLE).orElseThrow();
        Dataset urls = ingestService.createDataset("Example site", DatasetKind.USER_URLS);
        ingestService.addDocument(urls, "Home", "The garden gate still sticks after rain and the kettle takes its time.", "https://example.org/", "example.org", "general", null);
        ingestService.addDocument(urls, "About", "It is important to note that this page provides a comprehensive overview of the project in today's world.", "https://example.org/about", "example.org", "general", null);
        analysisService.analyze(urls.getId());
        var graph = analysisService.graph(urls.getId());
        @SuppressWarnings("unchecked")
        var nodes = (java.util.List<?>) graph.get("nodes");
        assertTrue(nodes.size() == 2);
        assertTrue(documents.countByDatasetId(wiki.getId()) > 2);
        var summary = analysisService.summary(urls.getId(), "documents");
        assertTrue(summary.containsKey("comparison"));
        @SuppressWarnings("unchecked")
        var comparison = (java.util.Map<String, Object>) summary.get("comparison");
        @SuppressWarnings("unchecked")
        var wikiHeadline = (java.util.Map<String, Object>) comparison.get("wikipediaSample");
        assertEquals("WIKIPEDIA_SAMPLE", wikiHeadline.get("kind"));
        assertTrue(wikiHeadline.get("documentCount") instanceof Number);
        assertTrue(((Number) wikiHeadline.get("documentCount")).longValue() > 20);
    }

    @Test
    void urlPagesShareSeedSourceAndReuseOneCollection() {
        Dataset stale = ingestService.createDataset("stale urls", DatasetKind.USER_URLS);
        ingestService.addDocument(stale, "Old", "The kettle still clicks after the water has already boiled on the stove.", "https://old.example/", "old.example", "general", null);
        Dataset extra = ingestService.createDataset("extra urls", DatasetKind.USER_URLS);
        ingestService.addDocument(extra, "Extra", "Rain on the porch roof made a small regular sound all afternoon.", "https://extra.example/", "extra.example", "general", null);

        UrlCrawlService.CrawlResult crawled = new UrlCrawlService.CrawlResult(
                List.of(
                        new UrlCrawlService.Page(
                                "https://example.org/",
                                "Home",
                                "The garden gate still sticks after rain and the kettle takes its time.",
                                "example.org",
                                "https://example.org/"),
                        new UrlCrawlService.Page(
                                "https://example.org/about",
                                "About",
                                "We left the windows open because the rooms needed air after painting.",
                                "example.org",
                                "https://example.org/"),
                        new UrlCrawlService.Page(
                                "https://other.org/",
                                "Other",
                                "It is important to note that this page provides a comprehensive overview of the project.",
                                "other.org",
                                "https://other.org/")
                ),
                2,
                1,
                0);
        Dataset ds = ingestService.persistCrawledUrls(null, crawled);
        assertEquals("example", ds.getName());
        assertEquals(DatasetKind.USER_URLS, ds.getKind());
        assertTrue(datasets.findByKindOrderByCreatedAtDesc(DatasetKind.USER_URLS).size() >= 3);
        Dataset again = ingestService.persistCrawledUrls(null, crawled);
        assertEquals(ds.getId(), again.getId());
        assertEquals("example", again.getName());
        List<Document> docs = documents.findByDatasetId(again.getId());
        assertEquals(3, docs.size());
        Map<String, String> byTitle = docs.stream().collect(Collectors.toMap(Document::getTitle, Document::getSource));
        assertEquals("https://example.org/", byTitle.get("Home"));
        assertEquals("https://example.org/", byTitle.get("About"));
        assertEquals("https://other.org/", byTitle.get("Other"));
    }
}
