package com.knowledgeos.controller;


import com.knowledgeos.model.Document;
import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.service.CrawlerService;
import com.knowledgeos.service.UrlQueueService;

import org.springframework.web.bind.annotation.*;

import java.util.List;


@RestController
public class DocumentController {


    private final DocumentRepository documentRepository;

    private final CrawlerService crawlerService;

    private final UrlQueueService urlQueueService;



    public DocumentController(
            DocumentRepository documentRepository,
            CrawlerService crawlerService,
            UrlQueueService urlQueueService
    ) {
        this.documentRepository = documentRepository;
        this.crawlerService = crawlerService;
        this.urlQueueService = urlQueueService;
    }



    @GetMapping("/documents")
    public List<Document> getDocuments(){

        return documentRepository.findAll();
    }



    @GetMapping("/queue/add")
    public String addToQueue(@RequestParam String url){

        urlQueueService.addUrl(url);

        return "Added: " + url;
    }




    @GetMapping("/crawl")
    public String crawlDocument(){


        UrlQueue queueItem = urlQueueService.getNextUrl();


        if(queueItem == null){

            return "Queue empty";
        }


        String content =
                crawlerService.crawl(queueItem.getUrl());



        Document document = new Document(
                "Crawled Document",
                queueItem.getUrl(),
                content
        );


        documentRepository.save(document);



        queueItem.setVisited(true);


        return "Crawled: " + queueItem.getUrl();
    }
}