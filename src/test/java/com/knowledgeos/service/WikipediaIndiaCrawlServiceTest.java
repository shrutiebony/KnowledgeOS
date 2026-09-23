package com.knowledgeos.service;

import org.junit.jupiter.api.Test;

import java.time.LocalDate;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class WikipediaIndiaCrawlServiceTest {

    @Test
    void followsMainNamespaceAndIndiaHubsOnly() {
        assertTrue(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/India"));
        assertTrue(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Mumbai"));
        assertTrue(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Category:India"));
        assertTrue(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Portal:India"));
        assertTrue(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Category:States_and_territories_of_India"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Special:Random"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/File:Flag_of_India.svg"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Talk:India"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/User:Example"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Wikipedia:About"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://en.wikipedia.org/wiki/Category:Living_people"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://fr.wikipedia.org/wiki/Inde"));
        assertFalse(WikipediaIndiaCrawlService.shouldFollow("https://example.org/wiki/India"));
    }

    @Test
    void extractsSameHostArticleLinksAndSkipsNamespaces() {
        String html = """
                <html><body>
                  <a href="/wiki/Mumbai">Mumbai</a>
                  <a href="/wiki/Special:WhatLinksHere/India">Special</a>
                  <a href="/wiki/File:Map.png">File</a>
                  <a href="/wiki/Talk:India">Talk</a>
                  <a href="/wiki/User:Pat">User</a>
                  <a href="/wiki/Wikipedia:Sandbox">Wikipedia</a>
                  <a href="https://en.wikipedia.org/wiki/Category:India">Category India</a>
                  <a href="https://de.wikipedia.org/wiki/Indien">Leave</a>
                </body></html>
                """;
        List<String> links = WikipediaIndiaCrawlService.extractWikiLinks("https://en.wikipedia.org/wiki/India", html);
        assertTrue(links.contains("https://en.wikipedia.org/wiki/Mumbai"));
        assertTrue(links.contains("https://en.wikipedia.org/wiki/Category:India"));
        assertFalse(links.stream().anyMatch(u -> u.contains("Special:")));
        assertFalse(links.stream().anyMatch(u -> u.contains("File:")));
        assertFalse(links.stream().anyMatch(u -> u.contains("Talk:")));
        assertFalse(links.stream().anyMatch(u -> u.contains("User:")));
        assertFalse(links.stream().anyMatch(u -> u.contains("Wikipedia:")));
        assertFalse(links.stream().anyMatch(u -> u.contains("de.wikipedia.org")));
    }

    @Test
    void parsesTitleTextCategoryAndLastmod() {
        String html = """
                <html><head><title>Mumbai - Wikipedia</title></head>
                <body>
                  <div id="mw-content-text"><div class="mw-parser-output">
                    <p>Mumbai is the capital of Maharashtra.</p>
                    <sup class="reference">[1]</sup>
                  </div></div>
                  <div id="mw-normal-catlinks"><ul>
                    <li><a>Cities in Maharashtra</a></li>
                    <li><a>Populated places in India</a></li>
                  </ul></div>
                  <li id="footer-info-lastmod">This page was last edited on 15 March 2024, at 08:12 (UTC).</li>
                </body></html>
                """;
        WikipediaIndiaCrawlService.WikiPage page =
                WikipediaIndiaCrawlService.parseArticle("https://en.wikipedia.org/wiki/Mumbai", html);
        assertEquals("Mumbai", page.title());
        assertTrue(page.text().contains("capital of Maharashtra"));
        assertFalse(page.text().contains("[1]"));
        assertEquals("Populated places in India", page.topic());
        assertEquals(LocalDate.of(2024, 3, 15), page.publishedAt());
        assertEquals("wikipedia.org", WikipediaIndiaCrawlService.SOURCE);
    }

    @Test
    void mobileHostCanonicalizesToDesktop() {
        assertEquals(
                "https://en.wikipedia.org/wiki/India",
                WikipediaIndiaCrawlService.canonicalizeWikiUrl("https://en.m.wikipedia.org/wiki/India#History"));
        assertTrue(WikipediaIndiaCrawlService.isEnWikipedia("https://en.m.wikipedia.org/wiki/India"));
        assertTrue(WikipediaIndiaCrawlService.isMainNamespaceArticle("https://en.wikipedia.org/wiki/India"));
        assertFalse(WikipediaIndiaCrawlService.isMainNamespaceArticle("https://en.wikipedia.org/wiki/Category:India"));
    }

    @Test
    void parsesHttpLastModifiedAndRevisionJson() {
        assertEquals(
                LocalDate.of(2024, 3, 15),
                WikipediaIndiaCrawlService.parseHttpLastModified("Fri, 15 Mar 2024 08:12:00 GMT"));
        assertEquals(
                LocalDate.of(2023, 11, 2),
                WikipediaIndiaCrawlService.parseRevisionTimestamp("2023-11-02T14:05:09Z"));
        String html = """
                <html><head>
                  <title>Kerala - Wikipedia</title>
                  <meta property="article:modified_time" content="2022-07-09T11:00:00Z" />
                </head>
                <body><div id="mw-content-text"><div class="mw-parser-output"><p>Kerala is a state.</p></div></div></body></html>
                """;
        WikipediaIndiaCrawlService.WikiPage page =
                WikipediaIndiaCrawlService.parseArticle("https://en.wikipedia.org/wiki/Kerala", html);
        assertEquals(LocalDate.of(2022, 7, 9), page.publishedAt());
        var dates = WikipediaIndiaCrawlService.parseRevisionQuery("""
                {"query":{"normalized":[{"from":"india","to":"India"}],
                  "pages":[{"title":"India","revisions":[{"timestamp":"2021-04-18T06:00:00Z"}]}]}}
                """);
        assertEquals(LocalDate.of(2021, 4, 18), dates.get("India"));
        assertEquals(LocalDate.of(2021, 4, 18), dates.get("india"));
    }
}
