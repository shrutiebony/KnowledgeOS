package com.knowledgeos.service;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.DatasetKind;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.GraphSageStatus;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentEdgeRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.apache.pdfbox.Loader;
import org.apache.pdfbox.pdmodel.PDDocument;
import org.apache.pdfbox.text.PDFTextStripper;
import org.jsoup.Jsoup;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.LocalDate;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Locale;
import java.util.Optional;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

@Service
public class IngestService {
    private final DatasetRepository datasets;
    private final DocumentRepository documents;
    private final DocumentEdgeRepository edges;
    private final StylometryService stylometryService;
    private final UrlCrawlService urlCrawlService;

    public IngestService(
            DatasetRepository datasets,
            DocumentRepository documents,
            DocumentEdgeRepository edges,
            StylometryService stylometryService,
            UrlCrawlService urlCrawlService) {
        this.datasets = datasets;
        this.documents = documents;
        this.edges = edges;
        this.stylometryService = stylometryService;
        this.urlCrawlService = urlCrawlService;
    }

    public record UrlIngestResult(
            Dataset dataset,
            int seedCount,
            int extraPages,
            int ingestedPages,
            int failedPages,
            int maxDepth,
            int maxPages) {}

    @Transactional
    public Dataset createDataset(String name, DatasetKind kind) {
        Dataset dataset = new Dataset();
        dataset.setName(name);
        dataset.setKind(kind);
        dataset.setCreatedAt(Instant.now());
        dataset.setAnalysisState("ingested");
        return datasets.save(dataset);
    }

    @Transactional
    public Document addDocument(Dataset dataset, String title, String text, String url, String source, String topic, LocalDate publishedAt) {
        String cleaned = text == null ? "" : text.trim();
        if (cleaned.isBlank()) {
            throw new IllegalArgumentException("Document has no extractable text: " + title);
        }
        Document doc = new Document();
        doc.setDatasetId(dataset.getId());
        doc.setTitle(title == null || title.isBlank() ? "Untitled" : title);
        doc.setText(cleaned);
        doc.setUrl(url);
        doc.setSource(source);
        doc.setTopic(topic);
        doc.setPublishedAt(publishedAt);
        doc.setWordCount(stylometryService.analyze(cleaned).wordCount());
        return documents.save(doc);
    }

    @Transactional
    public Dataset ingestUploads(String name, List<MultipartFile> files) throws IOException {
        List<String> filenames = new ArrayList<>();
        List<MultipartFile> present = new ArrayList<>();
        if (files != null) {
            for (MultipartFile file : files) {
                if (file == null || file.isEmpty()) {
                    continue;
                }
                present.add(file);
                filenames.add(file.getOriginalFilename() == null ? "upload" : file.getOriginalFilename());
            }
        }
        String datasetName = uniqueName(uploadCollectionName(name, filenames), DatasetKind.USER_UPLOAD);
        Dataset dataset = createDataset(datasetName, DatasetKind.USER_UPLOAD);
        for (MultipartFile file : present) {
            String filename = file.getOriginalFilename() == null ? "upload" : file.getOriginalFilename();
            String lower = filename.toLowerCase(Locale.ROOT);
            if (lower.endsWith(".zip")) {
                ingestZip(dataset, file);
            } else {
                ingestOne(dataset, filename, file.getBytes(), file.getContentType());
            }
        }
        if (documents.countByDatasetId(dataset.getId()) == 0) {
            throw new IllegalArgumentException("No extractable documents found in upload.");
        }
        return dataset;
    }

    public UrlIngestResult ingestUrls(String name, List<String> urls) {
        UrlCrawlService.CrawlResult crawled = urlCrawlService.crawl(urls);
        Dataset dataset = persistCrawledUrls(name, crawled);
        return new UrlIngestResult(
                dataset,
                crawled.seedCount(),
                crawled.extraPages(),
                crawled.pages().size(),
                crawled.failedPages(),
                urlCrawlService.getMaxDepth(),
                urlCrawlService.getMaxPages());
    }

    @Transactional
    public Dataset persistCrawledUrls(String requestedName, UrlCrawlService.CrawlResult crawled) {
        if (crawled == null || crawled.pages().isEmpty()) {
            throw new IllegalArgumentException("No text could be extracted from the provided URLs.");
        }
        String datasetName = urlCollectionName(requestedName, crawled);
        Dataset dataset = reuseOrCreateUrlCollection(datasetName);
        for (UrlCrawlService.Page page : crawled.pages()) {
            try {
                addDocument(
                        dataset,
                        page.title(),
                        page.text(),
                        clipUrl(page.url()),
                        sourceForSeed(page),
                        guessTopic(page.title() + " " + page.text()),
                        null);
            } catch (IllegalArgumentException ignored) {
                // skip pages that lost their text between crawl and persist
            }
        }
        if (documents.countByDatasetId(dataset.getId()) == 0) {
            throw new IllegalArgumentException("No text could be extracted from the provided URLs.");
        }
        return dataset;
    }

    private Dataset reuseOrCreateUrlCollection(String datasetName) {
        List<Dataset> sameName = datasets.findByKindAndNameIgnoreCaseOrderByCreatedAtDesc(DatasetKind.USER_URLS, datasetName);
        Dataset keep = sameName.isEmpty() ? null : sameName.get(0);
        for (int i = 1; i < sameName.size(); i++) {
            clearDatasetContents(sameName.get(i).getId());
            datasets.delete(sameName.get(i));
        }
        if (keep != null) {
            clearDatasetContents(keep.getId());
            keep.setName(datasetName);
            keep.setCreatedAt(Instant.now());
            keep.setLastAnalyzedAt(null);
            keep.setAnalysisState("ingested");
            keep.setGraphSageStatus(GraphSageStatus.NOT_TRAINED);
            keep.setGraphSageMessage(
                    "GraphSAGE representations may be computed as graph context. They are not a detection signal until a labeled classifier is trained and validated.");
            return datasets.save(keep);
        }
        return createDataset(uniqueName(datasetName, DatasetKind.USER_URLS), DatasetKind.USER_URLS);
    }

    @Transactional
    public void deleteDatasetAndContents(Dataset dataset) {
        if (dataset == null || dataset.getId() == null) {
            return;
        }
        clearDatasetContents(dataset.getId());
        datasets.delete(dataset);
        datasets.flush();
    }

    private void clearDatasetContents(Long datasetId) {
        edges.deleteByDatasetId(datasetId);
        documents.deleteByDatasetId(datasetId);
        documents.flush();
    }

    /**
     * One seed URL is one source. Child pages inherit that seed's URL.
     */
    static String sourceForSeed(UrlCrawlService.Page page) {
        if (page == null) {
            return "url";
        }
        if (page.seedUrl() != null && !page.seedUrl().isBlank()) {
            return clipUrl(page.seedUrl());
        }
        if (page.host() != null && !page.host().isBlank()) {
            return page.host();
        }
        String fromUrl = UrlCrawlService.hostOf(page.url());
        return fromUrl == null ? "url" : fromUrl;
    }

    static String urlCollectionName(String requestedName, UrlCrawlService.CrawlResult crawled) {
        String given = sanitizeDisplayName(requestedName);
        if (given != null) {
            return given;
        }
        String seed = firstSeedUrl(crawled);
        if (seed != null) {
            return nameFromSeedUrl(seed);
        }
        return "url";
    }

    static String uploadCollectionName(String requestedName, List<String> filenames) {
        String given = sanitizeDisplayName(requestedName);
        if (given != null) {
            return given;
        }
        List<String> bases = new ArrayList<>();
        if (filenames != null) {
            for (String filename : filenames) {
                String base = fileBaseName(filename);
                if (base != null && !bases.contains(base)) {
                    bases.add(base);
                }
            }
        }
        if (bases.size() == 1) {
            return bases.get(0);
        }
        if (bases.size() >= 2) {
            return bases.get(0) + "-" + bases.get(1);
        }
        return "upload";
    }

    static String nameFromSeedUrl(String seedUrl) {
        String host = UrlCrawlService.comparableHost(seedUrl);
        String hostShort = shortNameFromHost(host);
        String pathSlug = firstPathSlug(seedUrl);
        if (pathSlug != null && !pathSlug.equals(hostShort)) {
            return hostShort + "-" + pathSlug;
        }
        return hostShort;
    }

    static String shortNameFromHost(String host) {
        if (host == null || host.isBlank()) {
            return "url";
        }
        String value = host.toLowerCase(Locale.ROOT).trim();
        if (value.startsWith("www.")) {
            value = value.substring(4);
        }
        if (value.endsWith(".")) {
            value = value.substring(0, value.length() - 1);
        }
        String[] labels = value.split("\\.");
        if (labels.length == 0 || labels[0].isBlank()) {
            return "url";
        }
        String first = labels[0];
        for (String suffix : HOST_GENERIC_SUFFIXES) {
            if (first.length() > suffix.length() + 2 && first.endsWith(suffix)) {
                first = first.substring(0, first.length() - suffix.length());
                break;
            }
        }
        String cleaned = sanitizeToken(first);
        return cleaned == null ? "url" : cleaned;
    }

    static String firstPathSlug(String url) {
        String normalized = UrlCrawlService.normalizeUrl(url);
        if (normalized == null) {
            return null;
        }
        try {
            String path = URI.create(normalized).getPath();
            if (path == null || path.isBlank() || path.equals("/")) {
                return null;
            }
            for (String raw : path.split("/")) {
                if (raw == null || raw.isBlank()) {
                    continue;
                }
                String token = sanitizeToken(stripExt(raw));
                if (token == null || BORING_PATH.contains(token)) {
                    continue;
                }
                return token;
            }
        } catch (Exception ignored) {
            return null;
        }
        return null;
    }

    static String fileBaseName(String filename) {
        if (filename == null || filename.isBlank()) {
            return null;
        }
        String title = filename.replace('\\', '/');
        title = title.substring(title.lastIndexOf('/') + 1);
        return sanitizeToken(stripExt(title));
    }

    static String sanitizeDisplayName(String raw) {
        if (raw == null) {
            return null;
        }
        String value = COUNT_SUFFIX.matcher(raw.trim()).replaceAll("").trim();
        value = value.replaceAll("\\s+", " ");
        if (value.isBlank()) {
            return null;
        }
        return value;
    }

    static String sanitizeToken(String raw) {
        if (raw == null) {
            return null;
        }
        String value = COUNT_SUFFIX.matcher(raw.trim()).replaceAll("").toLowerCase(Locale.ROOT);
        value = value.replaceAll("[^a-z0-9]+", "-");
        value = value.replaceAll("^-+|-+$", "");
        if (value.isBlank()) {
            return null;
        }
        return value.length() <= 40 ? value : value.substring(0, 40);
    }

    private String uniqueName(String preferred, DatasetKind kind) {
        String base = preferred == null || preferred.isBlank() ? (kind == DatasetKind.USER_URLS ? "url" : "upload") : preferred;
        if (availableName(base, kind)) {
            return base;
        }
        int n = 2;
        while (!availableName(base + "-" + n, kind)) {
            n++;
            if (n > 1000) {
                return base + "-" + Instant.now().toEpochMilli();
            }
        }
        return base + "-" + n;
    }

    private boolean availableName(String name, DatasetKind kind) {
        return datasets.findFirstByNameIgnoreCase(name).isEmpty();
    }

    private static String firstSeedUrl(UrlCrawlService.CrawlResult crawled) {
        if (crawled == null || crawled.pages() == null) {
            return null;
        }
        for (UrlCrawlService.Page page : crawled.pages()) {
            if (page.seedUrl() != null && !page.seedUrl().isBlank()) {
                return page.seedUrl();
            }
        }
        for (UrlCrawlService.Page page : crawled.pages()) {
            if (page.url() != null && !page.url().isBlank()) {
                return page.url();
            }
        }
        return null;
    }

    private static final Pattern COUNT_SUFFIX = Pattern.compile("\\s*\\(\\d+\\)\\s*$");
    private static final List<String> HOST_GENERIC_SUFFIXES = List.of("project", "projects", "site", "online", "web");
    private static final Set<String> BORING_PATH = Set.of(
            "index", "home", "default", "www", "html", "page", "pages", "wiki");

    private void ingestZip(Dataset dataset, MultipartFile file) throws IOException {
        try (ZipInputStream zis = new ZipInputStream(file.getInputStream())) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                if (entry.isDirectory()) {
                    continue;
                }
                byte[] bytes = zis.readAllBytes();
                ingestOne(dataset, entry.getName(), bytes, null);
            }
        }
    }

    private void ingestOne(Dataset dataset, String filename, byte[] bytes, String contentType) throws IOException {
        String lower = filename.toLowerCase(Locale.ROOT);
        String title = filename.replace('\\', '/');
        title = title.substring(title.lastIndexOf('/') + 1);
        String text;
        if (lower.endsWith(".pdf") || (contentType != null && contentType.contains("pdf"))) {
            try (PDDocument pdf = Loader.loadPDF(bytes)) {
                text = new PDFTextStripper().getText(pdf);
            }
        } else if (lower.endsWith(".jsonl")) {
            ingestJsonl(dataset, bytes, filename);
            return;
        } else {
            text = new String(bytes, StandardCharsets.UTF_8);
            if (lower.endsWith(".html") || lower.endsWith(".htm")) {
                var html = Jsoup.parse(text);
                title = html.title().isBlank() ? title : html.title();
                text = html.text();
            }
        }
        addDocument(dataset, stripExt(title), text, null, filename, guessTopic(title + " " + text), null);
    }

    private void ingestJsonl(Dataset dataset, byte[] bytes, String source) {
        String[] lines = new String(bytes, StandardCharsets.UTF_8).split("\\R");
        for (String line : lines) {
            if (line.isBlank()) {
                continue;
            }
            String title = extractJson(line, "title");
            String text = extractJson(line, "text");
            String url = extractJson(line, "url");
            String topic = extractJson(line, "topic");
            String src = extractJson(line, "source");
            if (text.isBlank()) {
                continue;
            }
            addDocument(dataset, title.isBlank() ? "Untitled" : title, text, url, src.isBlank() ? source : src,
                    topic.isBlank() ? guessTopic(title + " " + text) : topic, null);
        }
    }

    private static String extractJson(String line, String key) {
        String needle = "\"" + key + "\"";
        int i = line.indexOf(needle);
        if (i < 0) {
            return "";
        }
        int colon = line.indexOf(':', i);
        int q1 = line.indexOf('"', colon + 1);
        if (q1 < 0) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        for (int p = q1 + 1; p < line.length(); p++) {
            char c = line.charAt(p);
            if (c == '\\' && p + 1 < line.length()) {
                sb.append(line.charAt(p + 1));
                p++;
                continue;
            }
            if (c == '"') {
                break;
            }
            sb.append(c);
        }
        return sb.toString();
    }

    public static String guessTopic(String text) {
        String lower = text.toLowerCase(Locale.ROOT);
        if (lower.contains("medicine") || lower.contains("clinical") || lower.contains("patient") || lower.contains("disease")) {
            return "medicine";
        }
        if (lower.contains("algorithm") || lower.contains("computer") || lower.contains("software") || lower.contains("graph")) {
            return "computer_science";
        }
        if (lower.contains("history") || lower.contains("empire") || lower.contains("war")) {
            return "history";
        }
        if (lower.contains("physics") || lower.contains("quantum") || lower.contains("particle")) {
            return "physics";
        }
        if (lower.contains("biology") || lower.contains("genome") || lower.contains("species")) {
            return "biology";
        }
        return "general";
    }

    private static String clipUrl(String url) {
        if (url == null) {
            return null;
        }
        return url.length() <= 2000 ? url : url.substring(0, 2000);
    }

    private static String stripExt(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }

    public List<String> topics(Long datasetId) {
        return documents.findDistinctTopicsByDatasetId(datasetId);
    }

    @Transactional
    public int applyPublishedAt(Map<Long, LocalDate> dates) {
        if (dates == null || dates.isEmpty()) {
            return 0;
        }
        int updated = 0;
        for (var e : dates.entrySet()) {
            if (e.getKey() == null || e.getValue() == null) {
                continue;
            }
            updated += documents.updatePublishedAtIfNull(e.getKey(), e.getValue());
        }
        return updated;
    }
}
