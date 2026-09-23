package com.knowledgeos.service;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.repository.DatasetRepository;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;
import org.springframework.transaction.annotation.Transactional;

import java.util.Locale;

/**
 * One-time-style prune of leftover collections. Keeps the stored Wikipedia India
 * corpus and any dataset named GDELT. Later uploads/crawls can still add new names
 * during the running process.
 */
@Component
@Order(20)
public class ExtraDatasetCleanup implements ApplicationRunner {
    private final DatasetRepository datasets;
    private final IngestService ingestService;

    public ExtraDatasetCleanup(DatasetRepository datasets, IngestService ingestService) {
        this.datasets = datasets;
        this.ingestService = ingestService;
    }

    @Override
    @Transactional
    public void run(ApplicationArguments args) {
        prune();
    }

    public int prune() {
        int removed = 0;
        for (Dataset dataset : datasets.findAll()) {
            if (keep(dataset.getName())) {
                continue;
            }
            ingestService.deleteDatasetAndContents(dataset);
            removed++;
        }
        return removed;
    }

    static boolean keep(String name) {
        if (name == null) {
            return false;
        }
        String value = name.trim();
        if (value.isEmpty()) {
            return false;
        }
        String lower = value.toLowerCase(Locale.ROOT);
        return lower.equals("wikipedia india") || lower.equals("gdelt");
    }
}
