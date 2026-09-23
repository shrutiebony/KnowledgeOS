package com.knowledgeos.service;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.model.DocumentMetaView;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;

import java.time.LocalDate;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Replaces the old 48-article Wikipedia sample with a crawled India Wikipedia corpus.
 * The live crawl runs in the background so the app can bind to 8080 first.
 */
@Component
@Order(10)
public class WikipediaIndiaIngestor implements ApplicationRunner {
    private static final Logger log = LoggerFactory.getLogger(WikipediaIndiaIngestor.class);

    private final DatasetRepository datasets;
    private final DocumentRepository documents;
    private final IngestService ingestService;
    private final AnalysisService analysisService;
    private final WikipediaIndiaCrawlService crawlService;
    private final boolean seed;
    private final boolean crawl;
    private final int flushEvery;

    public WikipediaIndiaIngestor(
            DatasetRepository datasets,
            DocumentRepository documents,
            IngestService ingestService,
            AnalysisService analysisService,
            WikipediaIndiaCrawlService crawlService,
            @Value("${knowledgeos.seed.wikipedia:true}") boolean seed,
            @Value("${knowledgeos.wikipedia-india.crawl:true}") boolean crawl,
            @Value("${knowledgeos.wikipedia-india.flush-every:25}") int flushEvery) {
        this.datasets = datasets;
        this.documents = documents;
        this.ingestService = ingestService;
        this.analysisService = analysisService;
        this.crawlService = crawlService;
        this.seed = seed;
        this.crawl = crawl;
        this.flushEvery = Math.max(1, flushEvery);
    }

    @Override
    public void run(ApplicationArguments args) {
        if (!seed) {
            return;
        }
        deleteLegacyWikipediaSample();
        Dataset dataset = existingIndiaDataset();
        if (!crawl) {
            if (dataset == null) {
                dataset = ingestService.createDataset(WikipediaIndiaCrawlService.DATASET_NAME, DatasetKind.WIKIPEDIA_SAMPLE);
                installOfflineFixture(dataset);
                analysisService.analyze(dataset.getId());
                log.info("Installed offline Wikipedia India fixture ({} documents). Live crawl is disabled.",
                        documents.countByDatasetId(dataset.getId()));
            } else {
                ensureAnalyzedAndDated(dataset.getId());
            }
            return;
        }
        if (dataset != null && crawlComplete(dataset)) {
            log.info("Wikipedia India already stored ({} pages). Ensuring analysis and dates.",
                    documents.countByDatasetId(dataset.getId()));
            ensureAnalyzedAndDated(dataset.getId());
            return;
        }
        if (dataset == null) {
            dataset = ingestService.createDataset(WikipediaIndiaCrawlService.DATASET_NAME, DatasetKind.WIKIPEDIA_SAMPLE);
        }
        final long datasetId = dataset.getId();
        markState(datasetId, "crawling");
        Thread worker = new Thread(() -> crawlAndAnalyze(datasetId), "wikipedia-india-ingest");
        worker.setDaemon(false);
        worker.start();
        log.info(
                "Started Wikipedia India crawl in the background (maxPages={}, depth={}, delay={}ms). Seeds: {}",
                crawlService.getMaxPages(),
                crawlService.getMaxDepth(),
                crawlService.getDelayMs(),
                WikipediaIndiaCrawlService.SEED_URLS);
    }

    private void crawlAndAnalyze(long datasetId) {
        Dataset dataset = datasets.findById(datasetId).orElse(null);
        if (dataset == null) {
            log.error("Wikipedia India dataset {} disappeared before crawl started.", datasetId);
            return;
        }
        Set<String> already = new LinkedHashSet<>();
        for (DocumentMetaView doc : documents.findMetaByDatasetId(datasetId)) {
            if (doc.url() != null && !doc.url().isBlank()) {
                already.add(doc.url());
            }
        }
        if (!already.isEmpty()) {
            log.info("Scoring {} already-stored Wikipedia India pages so the dashboard is not zeros.", already.size());
            ensureAnalyzedAndDated(datasetId);
        }
        log.info(
                "Crawling English Wikipedia (India seeds). Already stored={}, cap={}, depth={}, delay={}ms.",
                already.size(),
                crawlService.getMaxPages(),
                crawlService.getMaxDepth(),
                crawlService.getDelayMs());
        int[] persisted = {0};
        WikipediaIndiaCrawlService.CrawlStats stats;
        try {
            stats = crawlService.crawl(already, page -> {
                ingestService.addDocument(
                        dataset,
                        page.title(),
                        page.text(),
                        page.url(),
                        WikipediaIndiaCrawlService.SOURCE,
                        page.topic() == null ? "India" : page.topic(),
                        page.publishedAt());
                persisted[0]++;
                if (persisted[0] % flushEvery == 0) {
                    long total = documents.countByDatasetId(datasetId);
                    log.info("Wikipedia India progress: {} new pages this run, {} stored total. Analyzing incrementally.", persisted[0], total);
                    try {
                        analysisService.analyzeIncremental(datasetId);
                    } catch (Exception e) {
                        log.warn("Incremental Wikipedia India analysis failed: {}", e.getMessage());
                    }
                }
            });
        } catch (Exception e) {
            log.error("Wikipedia India crawl failed: {}", e.getMessage());
            markState(datasetId, "error");
            return;
        }
        long stored = documents.countByDatasetId(datasetId);
        if (stats.error() != null && stored == 0) {
            log.error("Wikipedia India crawl failed clearly: {}", stats.error());
            markState(datasetId, "error");
            return;
        }
        if (stored == 0) {
            log.error(
                    "Wikipedia India crawl stored 0 pages. Could not reach en.wikipedia.org. Seeds: {}",
                    WikipediaIndiaCrawlService.SEED_URLS);
            markState(datasetId, "error");
            return;
        }
        markState(datasetId, "ingested");
        log.info(
                "Wikipedia India crawl finished: {} pages stored ({} new this run, {} failed fetches, {} fetches). Analyzing.",
                stored,
                persisted[0],
                stats.failed(),
                stats.fetched());
        ensureAnalyzedAndDated(datasetId);
    }

    private void ensureAnalyzedAndDated(long datasetId) {
        if (documents.countByDatasetId(datasetId) <= 0) {
            return;
        }
        try {
            int dated = backfillDates(datasetId);
            if (dated > 0) {
                log.info("Backfilled publication dates on {} Wikipedia India pages.", dated);
            }
        } catch (Exception e) {
            log.warn("Wikipedia India date backfill failed: {}", e.getMessage());
        }
        try {
            analysisService.analyzeIncremental(datasetId);
            log.info("Wikipedia India analysis ready for {} pages.", documents.countByDatasetId(datasetId));
        } catch (Exception e) {
            log.error("Wikipedia India analysis failed: {}", e.getMessage());
            markState(datasetId, "error");
        }
    }

    private int backfillDates(long datasetId) {
        List<DocumentMetaView> missing = new ArrayList<>();
        for (DocumentMetaView doc : documents.findMetaByDatasetId(datasetId)) {
            if (doc.publishedAt() == null && doc.url() != null && !doc.url().isBlank()) {
                missing.add(doc);
            }
        }
        if (missing.isEmpty()) {
            return 0;
        }
        List<String> titles = new ArrayList<>();
        Map<String, List<Long>> idsByTitle = new LinkedHashMap<>();
        for (DocumentMetaView doc : missing) {
            String title = WikipediaIndiaCrawlService.wikiTitle(doc.url());
            if (title == null || title.isBlank()) {
                title = doc.title();
            }
            if (title == null || title.isBlank()) {
                continue;
            }
            String key = title.replace('_', ' ').trim();
            titles.add(key);
            idsByTitle.computeIfAbsent(key, k -> new ArrayList<>()).add(doc.id());
        }
        Map<String, LocalDate> revisions = crawlService.fetchLastRevisions(titles);
        Map<Long, LocalDate> dates = new LinkedHashMap<>();
        for (var e : revisions.entrySet()) {
            String want = e.getKey() == null ? "" : e.getKey().replace('_', ' ').trim();
            List<Long> ids = idsByTitle.get(want);
            if (ids == null) {
                String needle = want.toLowerCase(Locale.ROOT);
                for (var row : idsByTitle.entrySet()) {
                    if (row.getKey().toLowerCase(Locale.ROOT).equals(needle)) {
                        ids = row.getValue();
                        break;
                    }
                }
            }
            if (ids == null) {
                continue;
            }
            for (Long id : ids) {
                dates.put(id, e.getValue());
            }
        }
        return ingestService.applyPublishedAt(dates);
    }

    private void deleteLegacyWikipediaSample() {
        List<Dataset> all = datasets.findAll();
        for (Dataset dataset : all) {
            if (dataset.getKind() != DatasetKind.WIKIPEDIA_SAMPLE
                    && !isLegacySampleName(dataset.getName())) {
                continue;
            }
            if (isIndiaName(dataset.getName())) {
                continue;
            }
            log.info("Removing legacy Wikipedia dataset '{}' (kind {}).", dataset.getName(), dataset.getKind());
            ingestService.deleteDatasetAndContents(dataset);
        }
    }

    private Dataset existingIndiaDataset() {
        return datasets.findFirstByKindAndNameIgnoreCase(DatasetKind.WIKIPEDIA_SAMPLE, WikipediaIndiaCrawlService.DATASET_NAME)
                .or(() -> datasets.findFirstByNameIgnoreCase(WikipediaIndiaCrawlService.DATASET_NAME))
                .orElse(null);
    }

    private boolean crawlComplete(Dataset dataset) {
        long count = documents.countByDatasetId(dataset.getId());
        return count >= crawlService.getMaxPages();
    }

    private void markState(long datasetId, String state) {
        datasets.findById(datasetId).ifPresent(dataset -> {
            dataset.setAnalysisState(state);
            datasets.save(dataset);
        });
    }

    private static boolean isIndiaName(String name) {
        return name != null && name.trim().equalsIgnoreCase(WikipediaIndiaCrawlService.DATASET_NAME);
    }

    private static boolean isLegacySampleName(String name) {
        return name != null && name.trim().toLowerCase(Locale.ROOT).equals("wikipedia sample");
    }

    private void installOfflineFixture(Dataset dataset) {
        for (Fixture article : offlineArticles()) {
            ingestService.addDocument(
                    dataset,
                    article.title,
                    article.body,
                    "https://en.wikipedia.org/wiki/" + article.title.replace(' ', '_'),
                    WikipediaIndiaCrawlService.SOURCE,
                    article.topic,
                    article.date);
        }
    }

    private record Fixture(String title, String topic, LocalDate date, String body) {}

    /**
     * Tests and offline mode only. Not the old 48 formulaic computer-science/history pairs.
     */
    private static List<Fixture> offlineArticles() {
        return List.of(
                article("India", "Countries in India", 2024,
                        "India is a country in South Asia. It is the most populous country and the seventh-largest by area. "
                                + "The Indian subcontinent has been home to the Indus Valley Civilisation and later to successive empires. "
                                + "New Delhi is the capital; Mumbai, Kolkata, Chennai, and Bengaluru are major cities."),
                article("Mumbai", "Cities in India", 2023,
                        "Mumbai is the capital of Maharashtra and India's financial centre. The city grew from seven islands "
                                + "and is known for the film industry in Bollywood, the harbour, and a dense suburban railway."),
                article("Hindi", "Languages of India", 2022,
                        "Hindi is an Indo-Aryan language spoken across northern India. It is written in the Devanagari script "
                                + "and is one of the official languages of the Union government, alongside English."),
                article("Ganges", "Rivers of India", 2021,
                        "The Ganges rises in the Himalayas and flows across the North Indian plain into the Bay of Bengal. "
                                + "It is sacred in Hindu tradition and supports a large agricultural population along its basin."),
                article("Kerala", "States and union territories of India", 2023,
                        "Kerala is a state on the Malabar Coast. High literacy, a long coastline, and a history of spice trade "
                                + "with West Asia and Europe shape its modern reputation."),
                article("Tamil Nadu", "States and union territories of India", 2022,
                        "Tamil Nadu sits on the southeastern coast. Tamil is among the oldest continuously used languages, "
                                + "and Chennai is a major port and automobile manufacturing hub."),
                article("Rajasthan", "States and union territories of India", 2021,
                        "Rajasthan is India's largest state by area. The Thar Desert, Rajput forts, and cities such as Jaipur "
                                + "and Jodhpur draw visitors and dominate popular images of the region."),
                article("Bengaluru", "Cities in India", 2024,
                        "Bengaluru is the capital of Karnataka and a centre for information technology and public-sector research. "
                                + "The city's altitude gives it a milder climate than much of the Deccan."),
                article("Kolkata", "Cities in India", 2020,
                        "Kolkata, formerly Calcutta, was the capital of British India until 1911. It remains a cultural centre "
                                + "for Bengali literature, theatre, and politics on the Hooghly River."),
                article("Chennai", "Cities in India", 2022,
                        "Chennai is the capital of Tamil Nadu and a major port on the Coromandel Coast. "
                                + "Carnatic music, Tamil cinema, and automobile plants are local institutions."),
                article("Indian Railways", "Transport in India", 2023,
                        "Indian Railways is among the world's largest rail networks. It moves freight and passengers across "
                                + "gauge conversions, suburban systems, and long-distance expresses."),
                article("Monsoon", "Climate of India", 2021,
                        "The Indian monsoon is a seasonal reversal of winds that brings most of the country's annual rain "
                                + "between June and September. Agriculture still tracks its arrival."),
                article("Himalayas", "Mountain ranges of India", 2020,
                        "The Himalayas form India's northern wall. The range includes peaks in India, Nepal, Bhutan, and Tibet "
                                + "and feeds the Indus, Ganges, and Brahmaputra systems."),
                article("Indian independence movement", "History of India", 2019,
                        "The independence movement gathered mass politics under the Indian National Congress and other groups. "
                                + "Independence in 1947 was accompanied by Partition of British India."),
                article("Constitution of India", "Law of India", 2024,
                        "The Constitution of India came into force on 26 January 1950. It establishes a federal parliamentary "
                                + "republic with a long list of fundamental rights and directive principles."),
                article("Lok Sabha", "Politics of India", 2023,
                        "The Lok Sabha is the lower house of India's Parliament. Members are elected from constituencies "
                                + "for terms of up to five years unless the house is dissolved earlier."),
                article("Cricket in India", "Sport in India", 2022,
                        "Cricket is the most widely followed spectator sport in India. The Board of Control for Cricket in India "
                                + "runs the national team and the Indian Premier League."),
                article("Bollywood", "Cinema of India", 2021,
                        "Bollywood usually refers to the Hindi-language film industry based in Mumbai. "
                                + "Indian cinema as a whole also includes large Tamil, Telugu, Malayalam, and Bengali industries."),
                article("Ayurveda", "Medicine in India", 2020,
                        "Ayurveda is a traditional medical system with roots in South Asia. Classical texts discuss diet, "
                                + "herbal preparations, and humoral ideas that still appear in popular practice."),
                article("Indian cuisine", "Cuisine of India", 2022,
                        "Indian cuisine varies sharply by region. Rice and coconut on the coasts, wheat in the north, "
                                + "and spice blends such as garam masala appear in many home kitchens."),
                article("Sanskrit", "Languages of India", 2018,
                        "Sanskrit is a classical Indo-Aryan language of ancient India. It is the language of many Hindu, "
                                + "Buddhist, and Jain texts and the source of a large learned vocabulary in modern Indian languages."),
                article("Brahmaputra", "Rivers of India", 2021,
                        "The Brahmaputra flows from Tibet through Arunachal Pradesh and Assam into Bangladesh. "
                                + "Seasonal floods reshape the valley and the river's many channels."),
                article("Goa", "States and union territories of India", 2023,
                        "Goa is India's smallest state by area. A long Portuguese colonial period left churches, "
                                + "place names, and a coastline that is now a major tourist region."),
                article("Punjab, India", "States and union territories of India", 2022,
                        "Punjab in India is a major wheat-growing state. The Green Revolution changed yields, "
                                + "and Sikh history is closely tied to the region's cities and gurdwaras."),
                article("Hyderabad", "Cities in India", 2024,
                        "Hyderabad is the capital of Telangana. The old city around Charminar sits beside a large "
                                + "information-technology and pharmaceutical economy in the west of the urban area."),
                article("Indian Ocean", "Oceans", 2019,
                        "The Indian Ocean washes India's west and east coasts. Monsoon winds historically carried "
                                + "trade between East Africa, Arabia, and the Indian peninsula."));
    }

    private static Fixture article(String title, String topic, int year, String body) {
        return new Fixture(title, topic, LocalDate.of(year, 6, 15), body);
    }
}
