package com.knowledgeos.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.PageLink;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.PageLinkRepository;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.*;


@Service
public class WikipediaIngestionService {

    private static final String API_BASE = "https://en.wikipedia.org/w/api.php";
    private static final String USER_AGENT =
            "KnowledgeOS-Research/1.0 (static dataset ingestion for a research knowledge graph)";

    private final DocumentRepository documentRepository;
    private final PageLinkRepository pageLinkRepository;
    private final ChunkingService chunkingService;
    private final RestClient restClient;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public WikipediaIngestionService(
            DocumentRepository documentRepository,
            PageLinkRepository pageLinkRepository,
            ChunkingService chunkingService
    ) {
        this.documentRepository = documentRepository;
        this.pageLinkRepository = pageLinkRepository;
        this.chunkingService = chunkingService;

        HttpClient httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .build();

        JdkClientHttpRequestFactory factory = new JdkClientHttpRequestFactory(httpClient);
        factory.setReadTimeout(Duration.ofSeconds(30));

        this.restClient = RestClient.builder()
                .requestFactory(factory)
                .defaultHeader("User-Agent", USER_AGENT)
                .build();
    }

    public Map<String, Object> buildSnapshot(
            List<String> rootCategories,
            int maxDepth,
            int maxPages,
            String datasetKey
    ) {

        Set<String> titles = collectTitles(rootCategories, maxDepth, maxPages);

        int documentsCreated = 0;
        int documentsSkipped = 0;
        int edgesCreated = 0;

        for (String title : titles) {

            String url = articleUrl(title);

            if (documentRepository.existsByUrlAndDatasetKey(url, datasetKey)) {
                documentsSkipped++;
                continue;
            }

            PageContent page = fetchPage(title);

            if (page == null || page.text().isBlank()) {
                continue;
            }

            Document document = new Document(title, url, page.text(), datasetKey);
            Document saved = documentRepository.save(document);
            documentsCreated++;

            chunkingService.chunk(saved);

            for (String linkedTitle : page.links()) {

                if (linkedTitle.equals(title) || !titles.contains(linkedTitle)) {

                    continue;
                }

                pageLinkRepository.save(new PageLink(saved, articleUrl(linkedTitle)));
                edgesCreated++;
            }
        }

        return Map.of(
                "datasetKey", datasetKey,
                "titlesDiscovered", titles.size(),
                "documentsCreated", documentsCreated,
                "documentsAlreadyPresent", documentsSkipped,
                "edgesCreated", edgesCreated
        );
    }

    private Set<String> collectTitles(List<String> rootCategories, int maxDepth, int maxPages) {

        Set<String> titles = new LinkedHashSet<>();
        Set<String> visitedCategories = new HashSet<>();
        Deque<CategoryFrontierItem> frontier = new ArrayDeque<>();

        for (String c : rootCategories) {
            frontier.add(new CategoryFrontierItem(c, 0));
        }

        while (!frontier.isEmpty() && titles.size() < maxPages) {

            CategoryFrontierItem item = frontier.poll();

            if (visitedCategories.contains(item.category()) || item.depth() > maxDepth) {
                continue;
            }
            visitedCategories.add(item.category());

            JsonNode response = callApi(Map.of(
                    "action", "query",
                    "list", "categorymembers",
                    "cmtitle", "Category:" + item.category(),
                    "cmlimit", "500",
                    "format", "json"
            ));

            JsonNode members = response.path("query").path("categorymembers");

            for (JsonNode member : members) {

                int ns = member.path("ns").asInt();
                String title = member.path("title").asText();

                if (ns == 0) {
                    // Article namespace.
                    titles.add(title);
                    if (titles.size() >= maxPages) {
                        break;
                    }
                } else if (ns == 14 && item.depth() < maxDepth) {
                    // Subcategory namespace - recurse one level deeper.
                    frontier.add(new CategoryFrontierItem(
                            title.replaceFirst("^Category:", ""),
                            item.depth() + 1
                    ));
                }
            }
        }

        return titles;
    }

    private PageContent fetchPage(String title) {

        JsonNode response = callApi(Map.of(
                "action", "query",
                "prop", "extracts|links",
                "titles", title,
                "explaintext", "1",
                "exsectionformat", "plain",
                "pllimit", "500",
                "plnamespace", "0",
                "format", "json"
        ));

        JsonNode pages = response.path("query").path("pages");

        Iterator<String> pageIds = pages.fieldNames();
        if (!pageIds.hasNext()) {
            return null;
        }

        JsonNode page = pages.get(pageIds.next());

        String text = page.path("extract").asText("");

        List<String> links = new ArrayList<>();
        for (JsonNode link : page.path("links")) {
            links.add(link.path("title").asText());
        }
        return new PageContent(text, links);
    }

    private JsonNode callApi(Map<String, String> params) {

        StringBuilder uri = new StringBuilder(API_BASE).append("?");

        params.forEach((k, v) -> uri
                .append(k).append("=")
                .append(URLEncoder.encode(v, StandardCharsets.UTF_8))
                .append("&"));

        String raw = restClient.get()
                .uri(uri.toString())
                .retrieve()
                .body(String.class);

        try {
            return objectMapper.readTree(raw);
        } catch (Exception e) {
            throw new RuntimeException("Failed to parse Wikipedia API response for: " + uri, e);
        }
    }

    private String articleUrl(String title) {
        return "https://en.wikipedia.org/wiki/" + title.replace(" ", "_");
    }

    private record CategoryFrontierItem(String category, int depth) {}

    private record PageContent(String text, List<String> links) {}
}