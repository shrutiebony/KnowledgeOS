package com.knowledgeos.service;

import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.jsoup.nodes.Element;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;


@Service
public class CrawlerService {


    public String crawl(String url) {

        try {

            return Jsoup.connect(url)
                    .get()
                    .text();

        } catch (Exception e) {

            return "Failed to crawl: " + url;
        }
    }



    public List<String> extractLinks(String url) {

        List<String> links = new ArrayList<>();

        try {

            Document document = Jsoup.connect(url)
                    .get();


            for (Element link : document.select("a[href]")) {

                links.add(
                        link.absUrl("href")
                );
            }


        } catch (Exception e) {

            System.out.println("Failed extracting links from: " + url);
        }


        return links;
    }
}