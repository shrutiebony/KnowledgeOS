package com.knowledgeos.service;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class UrlCrawlServiceTest {

    @Test
    void normalizesFragmentsPortsAndTrailingSlash() {
        assertEquals(
                "https://example.org/docs",
                UrlCrawlService.normalizeUrl("https://example.org/docs/#intro"));
        assertEquals(
                "https://example.org/docs",
                UrlCrawlService.normalizeUrl("https://EXAMPLE.org:443/docs/"));
        assertEquals(
                "http://example.org/a",
                UrlCrawlService.normalizeUrl("http://example.org:80/a/"));
        assertEquals(
                UrlCrawlService.dedupKey("https://www.example.org/page"),
                UrlCrawlService.dedupKey("http://example.org/page/"));
    }

    @Test
    void extractsSameHostChildLinksOnly() {
        String html = """
                <html><body>
                  <a href="/about">About</a>
                  <a href="https://example.org/team/">Team</a>
                  <a href="https://www.example.org/jobs">Jobs</a>
                  <a href="https://other.org/escape">Leave</a>
                  <a href="https://example.org/file.pdf">PDF</a>
                  <a href="mailto:hi@example.org">Mail</a>
                  <a href="#top">Fragment</a>
                </body></html>
                """;
        List<String> links = UrlCrawlService.extractSameHostLinks("https://example.org/", html);
        assertTrue(links.contains("https://example.org/about"));
        assertTrue(links.contains("https://example.org/team"));
        assertTrue(links.contains("https://www.example.org/jobs") || links.contains("https://example.org/jobs"));
        assertFalse(links.stream().anyMatch(u -> u.contains("other.org")));
        assertFalse(links.stream().anyMatch(u -> u.endsWith(".pdf")));
        assertEquals(3, links.size());
    }

    @Test
    void treatsWwwAsSameHost() {
        assertTrue(UrlCrawlService.sameHost("https://www.example.org/a", "https://example.org/b"));
        assertFalse(UrlCrawlService.sameHost("https://example.org/a", "https://example.net/a"));
    }

    @Test
    void childPageSourceIsTheSeedUrl() {
        UrlCrawlService.Page child = new UrlCrawlService.Page(
                "https://example.org/about",
                "About",
                "text",
                "example.org",
                "https://example.org/");
        assertEquals("https://example.org/", IngestService.sourceForSeed(child));
        UrlCrawlService.Page otherSeed = new UrlCrawlService.Page(
                "https://example.org/team",
                "Team",
                "text",
                "example.org",
                "https://example.org/seed-two");
        assertEquals("https://example.org/seed-two", IngestService.sourceForSeed(otherSeed));
    }

    @Test
    void dropdownNamesComeFromHostTitleOrFilename() {
        assertEquals("gdelt", IngestService.shortNameFromHost("gdelt.utdallas.edu"));
        assertEquals("gdelt", IngestService.shortNameFromHost("www.gdeltproject.org"));
        assertEquals("example", IngestService.nameFromSeedUrl("https://example.org/"));
        assertEquals("example-about", IngestService.nameFromSeedUrl("https://example.org/about"));
        assertEquals("cover", IngestService.fileBaseName("cover.pdf"));
        assertEquals("cover", IngestService.fileBaseName("folder/cover.txt"));
        assertEquals("cover", IngestService.uploadCollectionName(null, List.of("cover.pdf")));
        assertEquals("notes", IngestService.uploadCollectionName("notes (24)", List.of("cover.pdf", "other.txt")));
        assertEquals("cover-other", IngestService.uploadCollectionName(null, List.of("cover.pdf", "other.txt")));
        assertEquals("gdelt", IngestService.sanitizeDisplayName("gdelt (1)"));
        UrlCrawlService.CrawlResult crawled = new UrlCrawlService.CrawlResult(
                List.of(new UrlCrawlService.Page(
                        "https://gdelt.utdallas.edu/",
                        "GDELT",
                        "text",
                        "gdelt.utdallas.edu",
                        "https://gdelt.utdallas.edu/")),
                1, 0, 0);
        assertEquals("gdelt", IngestService.urlCollectionName(null, crawled));
        assertEquals("My GDELT", IngestService.urlCollectionName("My GDELT (1)", crawled));
    }
}
