package com.knowledgeos.service;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.TestPropertySource;

import java.util.Set;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

@SpringBootTest
@TestPropertySource(properties = {
        "spring.datasource.url=jdbc:h2:mem:kos-cleanup-test;DB_CLOSE_DELAY=-1",
        "knowledgeos.seed.wikipedia=true",
        "knowledgeos.wikipedia-india.crawl=false"
})
class ExtraDatasetCleanupTest {
    @Autowired
    ExtraDatasetCleanup cleanup;
    @Autowired
    DatasetRepository datasets;
    @Autowired
    DocumentRepository documents;
    @Autowired
    IngestService ingestService;

    @Test
    void keepOnlyExactWikipediaIndiaAndGdeltNames() {
        assertTrue(ExtraDatasetCleanup.keep("Wikipedia India"));
        assertTrue(ExtraDatasetCleanup.keep("wikipedia india"));
        assertTrue(ExtraDatasetCleanup.keep("GDELT"));
        assertTrue(ExtraDatasetCleanup.keep("gdelt"));
        assertFalse(ExtraDatasetCleanup.keep("Wikipedia sample"));
        assertFalse(ExtraDatasetCleanup.keep("Wikipedia India / computer_science"));
        assertFalse(ExtraDatasetCleanup.keep("Medical articles"));
        assertFalse(ExtraDatasetCleanup.keep("CERN historic site"));
    }

    @Test
    void pruneDeletesExtrasButLeavesWikipediaAndGdelt() {
        Dataset extra = ingestService.createDataset("Medical articles", DatasetKind.USER_UPLOAD);
        ingestService.addDocument(extra, "Note", "Clinic notes from a rainy Tuesday stay in the drawer.", null, "clinic", "medicine", null);
        Dataset subset = ingestService.createDataset("Wikipedia India / computer_science", DatasetKind.WIKIPEDIA_SUBSET);
        ingestService.addDocument(subset, "Algo", "Graphs and hash tables showed up in the same lecture.", null, "wiki", "computer_science", null);
        Dataset gdelt = datasets.findFirstByNameIgnoreCase("GDELT").orElseGet(() ->
                ingestService.createDataset("GDELT", DatasetKind.USER_URLS));
        if (documents.countByDatasetId(gdelt.getId()) == 0) {
            ingestService.addDocument(gdelt, "Event", "A short GDELT event row for the cleanup test.", null, "gdelt", "general", null);
        }

        int removed = cleanup.prune();
        assertTrue(removed >= 2);

        Set<String> names = datasets.findAll().stream()
                .map(Dataset::getName)
                .collect(Collectors.toSet());
        assertTrue(names.stream().anyMatch(n -> n.equalsIgnoreCase("Wikipedia India")));
        assertTrue(names.stream().anyMatch(n -> n.equalsIgnoreCase("gdelt")));
        assertFalse(names.contains("Medical articles"));
        assertFalse(names.contains("Wikipedia India / computer_science"));
        assertEquals(2, names.size());
        assertTrue(documents.countByDatasetId(
                datasets.findFirstByNameIgnoreCase("Wikipedia India").orElseThrow().getId()) > 20);
        assertTrue(documents.countByDatasetId(gdelt.getId()) >= 1);
    }
}
