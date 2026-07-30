package com.knowledgeos.service;

import com.knowledgeos.model.CrawledPage;
import org.jsoup.Jsoup;
import org.jsoup.nodes.Element;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;


@Service
public class CrawlerService {


    public CrawledPage crawl(String url) {

        try {

            org.jsoup.nodes.Document document =
                    Jsoup.connect(url).get();


            return new CrawledPage(
                    document.title(),
                    document.text()
            );


        } catch(Exception e){

            return new CrawledPage(
                    "Failed",
                    ""
            );
        }
    }


    public List<String> extractLinks(String url) {

        List<String> links = new ArrayList<>();

        try {

            org.jsoup.nodes.Document document =
                    Jsoup.connect(url).get();


            for (Element link : document.select("a[href]")) {

                links.add(
                        link.absUrl("href")
                );
            }


        } catch (Exception e) {

            System.out.println(
                    "Failed extracting links from: " + url
            );
        }


        return links;
    }
}