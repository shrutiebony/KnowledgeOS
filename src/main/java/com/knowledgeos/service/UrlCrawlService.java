package com.knowledgeos.service;

import org.jsoup.Connection;
import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.jsoup.nodes.Element;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.net.URI;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

@Service
public class UrlCrawlService {
    private static final Pattern SKIP_EXT = Pattern.compile(
            "(?i)[.](?:pdf|docx?|xlsx?|pptx?|zip|rar|7z|tar|gz|tgz|png|jpe?g|gif|webp|svg|ico|bmp|"
                    + "mp3|mp4|m4a|wav|webm|avi|mov|wmv|css|js|mjs|map|json|woff2?|ttf|eot|otf|exe|dmg|apk|iso)(?:\\?|$)");
    private static final int MAX_BODY_BYTES = 1_500_000;

    private final int maxDepth;
    private final int maxPages;
    private final int timeoutMs;
    private final int delayMs;

    public UrlCrawlService(
            @Value("${knowledgeos.crawl.max-depth:2}") int maxDepth,
            @Value("${knowledgeos.crawl.max-pages:80}") int maxPages,
            @Value("${knowledgeos.crawl.timeout-ms:12000}") int timeoutMs,
            @Value("${knowledgeos.crawl.delay-ms:200}") int delayMs) {
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

    public record Page(String url, String title, String text, String host, String seedUrl) {}

    public record CrawlResult(List<Page> pages, int seedCount, int extraPages, int failedPages) {}

    public CrawlResult crawl(List<String> seedUrls) {
        record Item(String url, int depth, boolean seed, String seedUrl) {}

        ArrayDeque<Item> queue = new ArrayDeque<>();
        Set<String> seen = new LinkedHashSet<>();
        Set<String> seedKeys = new LinkedHashSet<>();
        List<Page> pages = new ArrayList<>();
        int seedCount = 0;
        int failedPages = 0;
        int fetches = 0;

        if (seedUrls != null) {
            for (String raw : seedUrls) {
                if (raw == null || raw.isBlank()) {
                    continue;
                }
                String normalized = normalizeUrl(raw);
                String key = dedupKey(normalized);
                if (key == null || !seen.add(key)) {
                    continue;
                }
                seedKeys.add(key);
                queue.add(new Item(normalized, 0, true, normalized));
                seedCount++;
            }
        }

        while (!queue.isEmpty() && fetches < maxPages) {
            Item item = queue.poll();
            if (fetches > 0) {
                pause();
            }
            fetches++;
            Fetched fetched;
            try {
                fetched = fetch(item.url);
            } catch (Exception e) {
                failedPages++;
                continue;
            }
            if (fetched == null) {
                failedPages++;
                continue;
            }
            String storedUrl = fetched.finalUrl() == null ? item.url : fetched.finalUrl();
            String storedKey = dedupKey(storedUrl);
            if (storedKey != null) {
                seen.add(storedKey);
                if (item.seed()) {
                    seedKeys.add(storedKey);
                } else if (!sameHost(item.url(), storedUrl)) {
                    failedPages++;
                    continue;
                }
            }
            if (fetched.text() != null && !fetched.text().isBlank()) {
                pages.add(new Page(
                        storedUrl,
                        fetched.title() == null || fetched.title().isBlank() ? storedUrl : fetched.title(),
                        fetched.text(),
                        hostOf(storedUrl),
                        item.seedUrl() == null ? storedUrl : item.seedUrl()));
            } else if (item.seed()) {
                failedPages++;
            }
            if (item.depth() >= maxDepth) {
                continue;
            }
            String linkBase = storedUrl;
            for (String link : fetched.links()) {
                String key = dedupKey(link);
                if (key == null || !seen.add(key)) {
                    continue;
                }
                if (!sameHost(linkBase, link)) {
                    seen.remove(key);
                    continue;
                }
                queue.add(new Item(link, item.depth() + 1, false, item.seedUrl()));
            }
        }

        int extraPages = 0;
        for (Page page : pages) {
            if (!seedKeys.contains(dedupKey(page.url()))) {
                extraPages++;
            }
        }
        return new CrawlResult(List.copyOf(pages), seedCount, extraPages, failedPages);
    }

    public static List<String> extractSameHostLinks(String pageUrl, String html) {
        String base = normalizeUrl(pageUrl);
        if (base == null || html == null) {
            return List.of();
        }
        return extractSameHostLinks(base, Jsoup.parse(html, base));
    }

    public static List<String> extractSameHostLinks(String pageUrl, Document html) {
        String base = normalizeUrl(pageUrl);
        if (base == null || html == null) {
            return List.of();
        }
        LinkedHashSet<String> out = new LinkedHashSet<>();
        for (Element anchor : html.select("a[href]")) {
            String href = anchor.hasAttr("abs:href") && !anchor.attr("abs:href").isBlank()
                    ? anchor.attr("abs:href")
                    : anchor.attr("href");
            String normalized = resolveAndNormalize(base, href);
            if (normalized == null || normalized.equals(base) || !sameHost(base, normalized) || skippable(normalized)) {
                continue;
            }
            out.add(normalized);
        }
        return new ArrayList<>(out);
    }

    public static String normalizeUrl(String raw) {
        if (raw == null) {
            return null;
        }
        String value = raw.trim();
        if (value.isEmpty()) {
            return null;
        }
        if (value.startsWith("//")) {
            value = "https:" + value;
        } else if (!value.contains("://")) {
            value = "https://" + value;
        }
        URI uri;
        try {
            uri = URI.create(value);
        } catch (Exception e) {
            return null;
        }
        String scheme = uri.getScheme();
        if (scheme == null) {
            return null;
        }
        scheme = scheme.toLowerCase(Locale.ROOT);
        if (!scheme.equals("http") && !scheme.equals("https")) {
            return null;
        }
        String host = uri.getHost();
        if (host == null || host.isBlank()) {
            return null;
        }
        host = host.toLowerCase(Locale.ROOT);
        if (host.endsWith(".")) {
            host = host.substring(0, host.length() - 1);
        }
        int port = uri.getPort();
        if ((port == 80 && scheme.equals("http")) || (port == 443 && scheme.equals("https"))) {
            port = -1;
        }
        String path = uri.getRawPath();
        if (path == null || path.isBlank()) {
            path = "/";
        }
        while (path.contains("//")) {
            path = path.replace("//", "/");
        }
        if (path.length() > 1 && path.endsWith("/")) {
            path = path.substring(0, path.length() - 1);
        }
        StringBuilder sb = new StringBuilder();
        sb.append(scheme).append("://").append(host);
        if (port != -1) {
            sb.append(':').append(port);
        }
        sb.append(path);
        if (uri.getRawQuery() != null && !uri.getRawQuery().isBlank()) {
            sb.append('?').append(uri.getRawQuery());
        }
        return sb.toString();
    }

    public static boolean sameHost(String urlA, String urlB) {
        String hostA = comparableHost(urlA);
        String hostB = comparableHost(urlB);
        return hostA != null && hostA.equals(hostB);
    }

    public static String comparableHost(String url) {
        String host = hostOf(url);
        if (host == null) {
            return null;
        }
        if (host.startsWith("www.")) {
            return host.substring(4);
        }
        return host;
    }

    public static String hostOf(String url) {
        String normalized = normalizeUrl(url);
        if (normalized == null) {
            return null;
        }
        try {
            return URI.create(normalized).getHost();
        } catch (Exception e) {
            return null;
        }
    }

    public static String dedupKey(String url) {
        String normalized = normalizeUrl(url);
        if (normalized == null) {
            return null;
        }
        String key = normalized.replaceFirst("(?i)^https?://", "");
        if (key.startsWith("www.")) {
            key = key.substring(4);
        }
        return key;
    }

    public static boolean skippable(String url) {
        if (url == null) {
            return true;
        }
        String path;
        try {
            path = URI.create(url).getPath();
        } catch (Exception e) {
            return true;
        }
        if (path == null) {
            return false;
        }
        return SKIP_EXT.matcher(path).find();
    }

    static String resolveAndNormalize(String base, String href) {
        if (href == null) {
            return null;
        }
        String value = href.trim();
        if (value.isEmpty()
                || value.startsWith("#")
                || startsWithIgnoreCase(value, "mailto:")
                || startsWithIgnoreCase(value, "javascript:")
                || startsWithIgnoreCase(value, "data:")
                || startsWithIgnoreCase(value, "tel:")) {
            return null;
        }
        try {
            URI resolved = URI.create(base).resolve(value);
            String normalized = normalizeUrl(resolved.toString());
            if (normalized == null || skippable(normalized)) {
                return null;
            }
            return normalized;
        } catch (Exception e) {
            return null;
        }
    }

    private Fetched fetch(String url) throws Exception {
        Connection.Response response = Jsoup.connect(url)
                .userAgent("KnowledgeOS/1.0 (local collection analysis)")
                .timeout(timeoutMs)
                .followRedirects(true)
                .ignoreHttpErrors(true)
                .maxBodySize(MAX_BODY_BYTES)
                .execute();
        if (response.statusCode() >= 400) {
            return null;
        }
        String contentType = response.contentType();
        if (contentType != null) {
            String type = contentType.toLowerCase(Locale.ROOT);
            if (type.contains("image/")
                    || type.contains("audio/")
                    || type.contains("video/")
                    || type.contains("application/pdf")
                    || type.contains("application/zip")
                    || type.contains("octet-stream")
                    || type.contains("font/")) {
                return null;
            }
        }
        Document html = response.parse();
        html.select("script, style, nav, footer, noscript").remove();
        String title = html.title();
        String text = html.body() == null ? html.text() : html.body().text();
        String finalUrl = response.url() == null ? url : normalizeUrl(response.url().toExternalForm());
        if (finalUrl == null) {
            finalUrl = url;
        }
        return new Fetched(finalUrl, title, text == null ? "" : text.trim(), extractSameHostLinks(finalUrl, html));
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

    private static boolean startsWithIgnoreCase(String value, String prefix) {
        return value.regionMatches(true, 0, prefix, 0, prefix.length());
    }

    private record Fetched(String finalUrl, String title, String text, List<String> links) {}
}
