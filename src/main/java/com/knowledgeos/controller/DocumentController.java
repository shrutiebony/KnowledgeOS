package com.knowledgeos.controller;


import com.knowledgeos.model.Document;
import com.knowledgeos.model.StatusResponse;
import com.knowledgeos.repository.DocumentChunkRepository;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.UrlQueueRepository;
import com.knowledgeos.service.UrlQueueService;
import com.knowledgeos.worker.CrawlerWorker;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;


@RestController
public class DocumentController {


    private final DocumentRepository documentRepository;

    private final DocumentChunkRepository documentChunkRepository;

    private final UrlQueueRepository urlQueueRepository;

    private final UrlQueueService urlQueueService;

    private final CrawlerWorker crawlerWorker;



    public DocumentController(
            DocumentRepository documentRepository,
            DocumentChunkRepository documentChunkRepository,
            UrlQueueRepository urlQueueRepository,
            UrlQueueService urlQueueService,
            CrawlerWorker crawlerWorker
    ) {
        this.documentRepository = documentRepository;
        this.documentChunkRepository = documentChunkRepository;
        this.urlQueueRepository = urlQueueRepository;
        this.urlQueueService = urlQueueService;
        this.crawlerWorker = crawlerWorker;
    }



    @GetMapping("/documents")
    public List<Document> getDocuments(){

        return documentRepository.findAll();
    }



    @GetMapping("/document/{id}")
    public ResponseEntity<Document> getDocument(@PathVariable Long id){

        return documentRepository.findById(id)
                .map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build());
    }



    @GetMapping("/queue/add")
    public String addToQueue(@RequestParam String url){

        urlQueueService.addUrl(url);

        return "Added: " + url;
    }




    @GetMapping("/crawl")
    public String crawlDocument(){

        return crawlerWorker.crawlNext();
    }



    @DeleteMapping("/queue/clear")
    public String clearQueue(){

        long removed = urlQueueRepository.deleteByVisitedFalse();

        return "Removed " + removed + " pending queue entries";
    }



    @GetMapping("/status")
    public StatusResponse status(){

        return new StatusResponse(
                documentRepository.count(),
                urlQueueRepository.countByVisitedFalse(),
                urlQueueRepository.countByVisitedTrue(),
                documentChunkRepository.count(),
                documentChunkRepository.countByEmbeddingIsNull()
        );
    }
}