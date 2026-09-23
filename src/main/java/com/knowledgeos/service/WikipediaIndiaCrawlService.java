package com.knowledgeos.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.jsoup.Connection;
import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.jsoup.nodes.Element;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.time.LocalDate;
import java.time.OffsetDateTime;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Same-host English Wikipedia crawl from India-focused seeds.
 * Caps are independent of the generic URL crawler (depth 2 / 80).
 */
@Service
public class WikipediaIndiaCrawlService {
    public static final String DATASET_NAME = "Wikipedia India";
    public static final String SOURCE = "wikipedia.org";
    public static final List<String> SEED_URLS = List.of(
            "https://en.wikipedia.org/wiki/India",
            "https://en.wikipedia.org/wiki/Portal:India",
            "https://en.wikipedia.org/wiki/Category:India");

    private static final String HOST = "en.wikipedia.org";
    private static final String USER_AGENT =
            "KnowledgeOS-WikiIndia/1.0 (local educational corpus ingest; en.wikipedia.org India collection)";
    private static final int MAX_BODY_BYTES = 5_000_000;
    private static final Set<String> SKIP_NAMESPACES = Set.of(
            "special", "file", "image", "talk", "user", "wikipedia", "wp",
            "help", "template", "module", "mediawiki", "draft", "timedtext",
            "media", "education_program", "gadget", "gadget_definition",
            "user_talk", "wikipedia_talk", "file_talk", "category_talk",
            "portal_talk", "template_talk", "help_talk", "draft_talk");
    private static final Set<String> HUB_NAMESPACES = Set.of("category", "portal");
    private static final Pattern LASTMOD = Pattern.compile(
            "last (?:edited|modified) on\\s+(\\d{1,2}\\s+\\w+\\s+\\d{4})", Pattern.CASE_INSENSITIVE);
    private static final Pattern LASTMOD_US = Pattern.compile(
            "last (?:edited|modified) on\\s+(\\w+\\s+\\d{1,2},\\s+\\d{4})", Pattern.CASE_INSENSITIVE);
    private static final DateTimeFormatter LASTMOD_FMT =
            DateTimeFormatter.ofPattern("d MMMM uuuu", Locale.ENGLISH);
    private static final DateTimeFormatter LASTMOD_ABBREV =
            DateTimeFormatter.ofPattern("d MMM uuuu", Locale.ENGLISH);
    private static final DateTimeFormatter LASTMOD_US_FMT =
            DateTimeFormatter.ofPattern("MMMM d, uuuu", Locale.ENGLISH);
    private static final ObjectMapper JSON = new ObjectMapper();

    private final int maxDepth;
    private final int maxPages;
    private final int timeoutMs;
    private final int delayMs;

    public WikipediaIndiaCrawlService(
            @Value("${knowledgeos.wikipedia-india.max-depth:4}") int maxDepth,
            @Value("${knowledgeos.wikipedia-india.max-pages:2000}") int maxPages,
            @Value("${knowledgeos.wikipedia-india.timeout-ms:15000}") int timeoutMs,
            @Value("${knowledgeos.wikipedia-india.delay-ms:300}") int delayMs) {
        this.maxDepth = Math.max(0, maxDepth);
        this.maxPages = Math.max(1, maxPages);
        this.timeoutMs = Math.max(1000, timeoutMs);
        this.delayMs = Math.max(0, delayMs);
    }

    public int getMaxDepth() {
        return maxDepth;
    }

    public int getMaxPages() {
        return maxPages;
    }

    public int getDelayMs() {
        return delayMs;
    }

    public record WikiPage(String url, String title, String text, String topic, LocalDate publishedAt) {}

    public record CrawlStats(int stored, int failed, int fetched, String error) {}

    public CrawlStats crawl(Set<String> alreadyStoredUrls, Consumer<WikiPage> onPage) {
        record Item(String url, int depth, boolean seed) {}

        ArrayDeque<Item> queue = new ArrayDeque<>();
        Set<String> seen = new LinkedHashSet<>();
        Set<String> storedKeys = new LinkedHashSet<>();
        if (alreadyStoredUrls != null) {
            for (String url : alreadyStoredUrls) {
                String key = UrlCrawlService.dedupKey(canonicalizeWikiUrl(url));
                if (key != null) {
                    storedKeys.add(key);
                }
            }
        }

        for (String seed : SEED_URLS) {
            String normalized = canonicalizeWikiUrl(seed);
            String key = UrlCrawlService.dedupKey(normalized);
            if (key == null) {
                continue;
            }
            seen.add(key);
            queue.add(new Item(normalized, 0, true));
        }

        int stored = storedKeys.size();
        int failed = 0;
        int fetched = 0;
        int seedFailures = 0;
        String lastError = null;

        while (!queue.isEmpty() && stored < maxPages) {
            if (Thread.currentThread().isInterrupted()) {
                lastError = "Crawl interrupted";
                break;
            }
            Item item = queue.poll();
            if (fetched > 0) {
                pause();
            }
            fetched++;
            Fetched page;
            try {
                page = fetch(item.url());
            } catch (Exception e) {
                failed++;
                lastError = e.getMessage();
                if (item.seed()) {
                    seedFailures++;
                }
                continue;
            }
            if (page == null) {
                failed++;
                if (item.seed()) {
                    seedFailures++;
                }
                continue;
            }

            String storedUrl = page.finalUrl() == null ? item.url() : page.finalUrl();
            if (!isEnWikipedia(storedUrl)) {
                failed++;
                continue;
            }
            String storedKey = UrlCrawlService.dedupKey(storedUrl);
            if (storedKey != null) {
                seen.add(storedKey);
            }

            boolean storeable = isMainNamespaceArticle(storedUrl);
            if (storeable && page.text() != null && !page.text().isBlank()) {
                boolean already = storedKey != null && storedKeys.contains(storedKey);
                if (!already) {
                    onPage.accept(new WikiPage(
                            storedUrl,
                            page.title(),
                            page.text(),
                            page.topic(),
                            page.publishedAt()));
                    stored++;
                    if (storedKey != null) {
                        storedKeys.add(storedKey);
                    }
                    if (alreadyStoredUrls != null) {
                        alreadyStoredUrls.add(storedUrl);
                    }
                }
            } else if (item.seed() && storeable) {
                failed++;
                seedFailures++;
                lastError = "Seed page had no extractable article text: " + storedUrl;
            }

            if (item.depth() >= maxDepth) {
                continue;
            }
            for (String link : page.links()) {
                String key = UrlCrawlService.dedupKey(link);
                if (key == null || !seen.add(key)) {
                    continue;
                }
                if (!shouldFollow(link)) {
                    seen.remove(key);
                    continue;
                }
                queue.add(new Item(link, item.depth() + 1, false));
            }
        }

        if (stored == 0 && (alreadyStoredUrls == null || alreadyStoredUrls.isEmpty())) {
            String reason = lastError == null
                    ? "no article text could be extracted"
                    : lastError;
            return new CrawlStats(0, failed, fetched,
                    "Could not reach English Wikipedia (en.wikipedia.org) from the India seeds. "
                            + "Network may be blocking Wikipedia. "
                            + "Seeds: " + String.join(", ", SEED_URLS)
                            + ". Last error: " + reason
                            + ". Seed failures: " + seedFailures + ".");
        }
        return new CrawlStats(stored, failed, fetched, null);
    }

    public static List<String> extractWikiLinks(String pageUrl, String html) {
        if (html == null) {
            return List.of();
        }
        String base = canonicalizeWikiUrl(pageUrl);
        return extractWikiLinks(base, Jsoup.parse(html, base == null ? pageUrl : base));
    }

    public static List<String> extractWikiLinks(String pageUrl, Document html) {
        LinkedHashSet<String> out = new LinkedHashSet<>();
        if (html == null) {
            return List.of();
        }
        String base = canonicalizeWikiUrl(pageUrl);
        if (base == null) {
            return List.of();
        }
        for (Element anchor : html.select("a[href]")) {
            String href = anchor.hasAttr("abs:href") && !anchor.attr("abs:href").isBlank()
                    ? anchor.attr("abs:href")
                    : anchor.attr("href");
            String normalized = resolveWikiLink(base, href);
            if (normalized != null && shouldFollow(normalized) && !normalized.equals(base)) {
                out.add(normalized);
            }
        }
        return new ArrayList<>(out);
    }

    public static WikiPage parseArticle(String pageUrl, String html) {
        if (html == null) {
            return null;
        }
        String base = canonicalizeWikiUrl(pageUrl);
        Document doc = Jsoup.parse(html, base == null ? pageUrl : base);
        return parseArticle(base == null ? pageUrl : base, doc);
    }

    public static WikiPage parseArticle(String pageUrl, Document html) {
        if (html == null) {
            return null;
        }
        String url = canonicalizeWikiUrl(pageUrl);
        if (url == null) {
            url = pageUrl;
        }
        String title = articleTitle(html.title());
        String text = articleText(html);
        if (text == null || text.isBlank()) {
            return null;
        }
        return new WikiPage(url, title, text, topicFromCategories(html), publishedAt(html, null));
    }

    public static boolean shouldFollow(String url) {
        String canonical = canonicalizeWikiUrl(url);
        if (canonical == null || !isEnWikipedia(canonical) || !isWikiPath(canonical)) {
            return false;
        }
        String title = wikiTitle(canonical);
        if (title == null || title.isBlank() || skipNamespace(title)) {
            return false;
        }
        if (isHubNamespace(title)) {
            return title.toLowerCase(Locale.ROOT).contains("india");
        }
        return true;
    }

    public static boolean isMainNamespaceArticle(String url) {
        String title = wikiTitle(canonicalizeWikiUrl(url));
        return title != null && !title.isBlank() && !skipNamespace(title) && !isHubNamespace(title);
    }

    public static boolean isEnWikipedia(String url) {
        String host = UrlCrawlService.hostOf(canonicalizeWikiUrl(url));
        return HOST.equalsIgnoreCase(host);
    }

    public static String canonicalizeWikiUrl(String raw) {
        String normalized = UrlCrawlService.normalizeUrl(raw);
        if (normalized == null) {
            return null;
        }
        URI uri;
        try {
            uri = URI.create(normalized);
        } catch (Exception e) {
            return null;
        }
        String host = uri.getHost();
        if (host == null) {
            return null;
        }
        host = host.toLowerCase(Locale.ROOT);
        if (host.startsWith("www.")) {
            host = host.substring(4);
        }
        if (host.equals("en.m.wikipedia.org")) {
            host = HOST;
        }
        if (!host.equals(HOST)) {
            return normalized;
        }
        String path = uri.getRawPath();
        if (path == null || path.isBlank()) {
            path = "/";
        }
        return "https://" + HOST + path;
    }

    static String resolveWikiLink(String base, String href) {
        if (href == null) {
            return null;
        }
        String value = href.trim();
        if (value.isEmpty()
                || value.startsWith("#")
                || value.regionMatches(true, 0, "mailto:", 0, 7)
                || value.regionMatches(true, 0, "javascript:", 0, 11)) {
            return null;
        }
        try {
            URI resolved = URI.create(base).resolve(value);
            String canonical = canonicalizeWikiUrl(resolved.toString());
            if (canonical == null || !shouldFollow(canonical)) {
                return null;
            }
            return canonical;
        } catch (Exception e) {
            return null;
        }
    }

    static boolean isWikiPath(String url) {
        try {
            String path = URI.create(url).getPath();
            return path != null && path.startsWith("/wiki/") && path.length() > "/wiki/".length();
        } catch (Exception e) {
            return false;
        }
    }

    static String wikiTitle(String url) {
        if (url == null) {
            return null;
        }
        try {
            String path = URI.create(url).getPath();
            if (path == null || !path.startsWith("/wiki/")) {
                return null;
            }
            String title = path.substring("/wiki/".length());
            return java.net.URLDecoder.decode(title.replace('+', ' '), java.nio.charset.StandardCharsets.UTF_8);
        } catch (Exception e) {
            return null;
        }
    }

    static boolean skipNamespace(String title) {
        String ns = namespace(title);
        if (ns.isEmpty()) {
            return false;
        }
        if (ns.endsWith("_talk") || ns.equals("talk")) {
            return true;
        }
        return SKIP_NAMESPACES.contains(ns);
    }

    static boolean isHubNamespace(String title) {
        return HUB_NAMESPACES.contains(namespace(title));
    }

    static String namespace(String title) {
        if (title == null) {
            return "";
        }
        int colon = title.indexOf(':');
        if (colon <= 0) {
            return "";
        }
        return title.substring(0, colon).toLowerCase(Locale.ROOT).replace(' ', '_');
    }

    static String articleTitle(String raw) {
        if (raw == null || raw.isBlank()) {
            return "Untitled";
        }
        String title = raw.trim();
        if (title.endsWith(" - Wikipedia")) {
            title = title.substring(0, title.length() - " - Wikipedia".length()).trim();
        }
        return title.isBlank() ? "Untitled" : title;
    }

    static String articleText(Document html) {
        Element content = html.selectFirst("#mw-content-text .mw-parser-output");
        if (content == null) {
            content = html.selectFirst("#mw-content-text");
        }
        if (content == null) {
            content = html.body();
        }
        if (content == null) {
            return "";
        }
        content = content.clone();
        content.select("script, style, noscript, .mw-editsection, sup.reference, .reflist, "
                + ".mw-references-wrap, .navbox, .vertical-navbox, #toc, .toc").remove();
        String text = content.text();
        return text == null ? "" : text.trim();
    }

    static String topicFromCategories(Document html) {
        String fallback = null;
        for (Element link : html.select("#mw-normal-catlinks ul li a")) {
            String cat = link.text();
            if (cat == null || cat.isBlank()) {
                continue;
            }
            String clipped = clip(cat.trim(), 120);
            if (fallback == null) {
                fallback = clipped;
            }
            if (cat.toLowerCase(Locale.ROOT).contains("india")) {
                return clipped;
            }
        }
        return fallback == null ? "India" : fallback;
    }

    static LocalDate publishedAt(Document html) {
        return publishedAt(html, null);
    }

    static LocalDate publishedAt(Document html, String lastModifiedHeader) {
        if (html != null) {
            LocalDate fromFooter = parseLastmodText(footerBlob(html));
            if (fromFooter != null) {
                return fromFooter;
            }
            for (String selector : List.of(
                    "meta[property=article:modified_time]",
                    "meta[property=og:updated_time]",
                    "meta[name=last-modified]",
                    "meta[property=article:published_time]")) {
                Element meta = html.selectFirst(selector);
                if (meta == null) {
                    continue;
                }
                LocalDate fromMeta = parseIsoDate(meta.attr("content"));
                if (fromMeta != null) {
                    return fromMeta;
                }
            }
            Element time = html.selectFirst("time[datetime]");
            if (time != null) {
                LocalDate fromTime = parseIsoDate(time.attr("datetime"));
                if (fromTime != null) {
                    return fromTime;
                }
            }
        }
        return parseHttpLastModified(lastModifiedHeader);
    }

    static String footerBlob(Document html) {
        Element lastmod = html.selectFirst("#footer-info-lastmod, .mw-last-modified, #mw-revision-date");
        if (lastmod != null && lastmod.hasText()) {
            return lastmod.text();
        }
        Element parser = html.selectFirst("#mw-content-text .mw-parser-output");
        if (parser != null) {
            String text = parser.text();
            if (text != null && LASTMOD.matcher(text).find()) {
                return text;
            }
        }
        return html.text();
    }

    static LocalDate parseLastmodText(String blob) {
        if (blob == null || blob.isBlank()) {
            return null;
        }
        Matcher matcher = LASTMOD.matcher(blob);
        if (matcher.find()) {
            LocalDate parsed = parseFlexibleDate(matcher.group(1));
            if (parsed != null) {
                return parsed;
            }
        }
        Matcher us = LASTMOD_US.matcher(blob);
        if (us.find()) {
            return parseFlexibleDate(us.group(1));
        }
        return null;
    }

    static LocalDate parseFlexibleDate(String raw) {
        if (raw == null || raw.isBlank()) {
            return null;
        }
        String value = raw.trim();
        for (DateTimeFormatter fmt : List.of(LASTMOD_FMT, LASTMOD_ABBREV, LASTMOD_US_FMT)) {
            try {
                return LocalDate.parse(value, fmt);
            } catch (DateTimeParseException ignored) {
                // try the next Wikipedia / HTTP date style
            }
        }
        return parseIsoDate(value);
    }

    static LocalDate parseHttpLastModified(String header) {
        if (header == null || header.isBlank()) {
            return null;
        }
        String value = header.trim();
        try {
            return ZonedDateTime.parse(value, DateTimeFormatter.RFC_1123_DATE_TIME).toLocalDate();
        } catch (DateTimeParseException ignored) {
            return parseIsoDate(value);
        }
    }

    static LocalDate parseIsoDate(String raw) {
        if (raw == null || raw.isBlank()) {
            return null;
        }
        String value = raw.trim();
        try {
            if (value.length() >= 10 && value.charAt(4) == '-' && value.charAt(7) == '-') {
                if (value.length() == 10) {
                    return LocalDate.parse(value);
                }
                try {
                    return OffsetDateTime.parse(value).toLocalDate();
                } catch (DateTimeParseException ignored) {
                    return LocalDate.parse(value.substring(0, 10));
                }
            }
        } catch (DateTimeParseException ignored) {
            return null;
        }
        return null;
    }

    static LocalDate parseRevisionTimestamp(String iso) {
        return parseIsoDate(iso);
    }

    public Map<String, LocalDate> fetchLastRevisions(Collection<String> titles) {
        Map<String, LocalDate> out = new LinkedHashMap<>();
        if (titles == null || titles.isEmpty()) {
            return out;
        }
        List<String> unique = new ArrayList<>();
        Set<String> seen = new LinkedHashSet<>();
        for (String title : titles) {
            if (title == null || title.isBlank()) {
                continue;
            }
            String key = title.replace('_', ' ').trim();
            if (seen.add(key.toLowerCase(Locale.ROOT))) {
                unique.add(key);
            }
        }
        for (int i = 0; i < unique.size(); i += 50) {
            List<String> batch = unique.subList(i, Math.min(i + 50, unique.size()));
            try {
                out.putAll(queryLastRevisions(batch));
            } catch (Exception ignored) {
                // Keep dates we already have; remaining titles stay undated.
            }
            if (i + 50 < unique.size()) {
                pause();
            }
        }
        return out;
    }

    private Map<String, LocalDate> queryLastRevisions(List<String> titles) throws Exception {
        String joined = String.join("|", titles);
        String api = "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                + "&prop=revisions&rvprop=timestamp&redirects=1&titles="
                + URLEncoder.encode(joined, StandardCharsets.UTF_8);
        String body = Jsoup.connect(api)
                .userAgent(USER_AGENT)
                .timeout(timeoutMs)
                .ignoreContentType(true)
                .header("Accept", "application/json")
                .execute()
                .body();
        return parseRevisionQuery(body);
    }

    static Map<String, LocalDate> parseRevisionQuery(String json) {
        Map<String, LocalDate> out = new LinkedHashMap<>();
        if (json == null || json.isBlank()) {
            return out;
        }
        try {
            JsonNode root = JSON.readTree(json).path("query");
            Map<String, String> aliases = new LinkedHashMap<>();
            for (JsonNode n : root.path("normalized")) {
                String from = n.path("from").asText("");
                String to = n.path("to").asText("");
                if (!from.isBlank() && !to.isBlank()) {
                    aliases.put(from, to);
                }
            }
            for (JsonNode n : root.path("redirects")) {
                String from = n.path("from").asText("");
                String to = n.path("to").asText("");
                if (!from.isBlank() && !to.isBlank()) {
                    aliases.put(from, to);
                }
            }
            Map<String, LocalDate> byCanonical = new LinkedHashMap<>();
            for (JsonNode page : root.path("pages")) {
                if (page.path("missing").asBoolean(false)) {
                    continue;
                }
                String title = page.path("title").asText("");
                JsonNode revisions = page.path("revisions");
                if (!revisions.isArray() || revisions.isEmpty()) {
                    continue;
                }
                LocalDate date = parseRevisionTimestamp(revisions.get(0).path("timestamp").asText(null));
                if (date != null && !title.isBlank()) {
                    byCanonical.put(title, date);
                    out.put(title, date);
                }
            }
            for (var e : aliases.entrySet()) {
                LocalDate date = byCanonical.get(e.getValue());
                if (date != null) {
                    out.put(e.getKey(), date);
                }
            }
        } catch (Exception ignored) {
            return out;
        }
        return out;
    }

    private Fetched fetch(String url) throws Exception {
        Connection.Response response = connect(url);
        if (response.statusCode() == 429 || response.statusCode() == 503) {
            pause();
            response = connect(url);
        }
        if (response.statusCode() >= 400) {
            throw new IllegalStateException("HTTP " + response.statusCode() + " for " + url);
        }
        String contentType = response.contentType();
        if (contentType != null && !contentType.toLowerCase(Locale.ROOT).contains("html")) {
            return null;
        }
        Document html = response.parse();
        String finalUrl = response.url() == null ? url : canonicalizeWikiUrl(response.url().toExternalForm());
        if (finalUrl == null) {
            finalUrl = url;
        }
        LocalDate headerDate = parseHttpLastModified(response.header("Last-Modified"));
        WikiPage parsed = parseArticle(finalUrl, html);
        List<String> links = extractWikiLinks(finalUrl, html);
        if (parsed == null) {
            return new Fetched(finalUrl, articleTitle(html.title()), "", "India", headerDate, links);
        }
        LocalDate date = parsed.publishedAt() != null ? parsed.publishedAt() : headerDate;
        if (date == null) {
            date = publishedAt(html, response.header("Last-Modified"));
        }
        return new Fetched(finalUrl, parsed.title(), parsed.text(), parsed.topic(), date, links);
    }

    private Connection.Response connect(String url) throws Exception {
        return Jsoup.connect(url)
                .userAgent(USER_AGENT)
                .timeout(timeoutMs)
                .followRedirects(true)
                .ignoreHttpErrors(true)
                .maxBodySize(MAX_BODY_BYTES)
                .header("Accept", "text/html,application/xhtml+xml")
                .header("Accept-Language", "en")
                .execute();
    }

    private void pause() {
        if (delayMs <= 0) {
            return;
        }
        try {
            Thread.sleep(delayMs);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static String clip(String value, int max) {
        if (value.length() <= max) {
            return value;
        }
        return value.substring(0, max);
    }

    private record Fetched(
            String finalUrl,
            String title,
            String text,
            String topic,
            LocalDate publishedAt,
            List<String> links) {}
}
