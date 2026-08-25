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
@CrossOrigin(origins = "*")
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
    public List<Document> getDocuments(
            @RequestParam(required = false) String datasetKey
    ){

        return datasetKey == null
                ? documentRepository.findAll()
                : documentRepository.findByDatasetKey(datasetKey);
    }


    @GetMapping("/datasets")
    public List<String> getDatasets(){

        return documentRepository.findDistinctDatasetKeys();
    }



    @GetMapping("/document/{id}")
    public ResponseEntity<Document> getDocument(@PathVariable Long id){

        return documentRepository.findById(id)
                .map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build());
    }


    @GetMapping("/queue/add")
    public String addToQueue(
            @RequestParam String url,
            @RequestParam(defaultValue = "default") String datasetKey
    ){

        urlQueueService.addUrl(url, datasetKey);

        return "Added to dataset '" + datasetKey + "': " + url;
    }




    @GetMapping("/crawl")
    public String crawlDocument(
            @RequestParam(defaultValue = "default") String datasetKey
    ){
        return crawlerWorker.crawlNext(datasetKey);
    }



    @DeleteMapping("/queue/clear")
    public String clearQueue(
            @RequestParam(defaultValue = "default") String datasetKey
    ){

        long removed = urlQueueRepository.deleteByDatasetKeyAndVisitedFalse(datasetKey);

        return "Removed " + removed + " pending queue entries from dataset '" + datasetKey + "'";
    }



    @GetMapping("/status")
    public StatusResponse status(
            @RequestParam(defaultValue = "default") String datasetKey
    ){

        return new StatusResponse(
                documentRepository.countByDatasetKey(datasetKey),
                urlQueueRepository.countByDatasetKeyAndVisitedFalse(datasetKey),
                urlQueueRepository.countByDatasetKeyAndVisitedTrue(datasetKey),
                documentChunkRepository.count(),
                documentChunkRepository.countByEmbeddingIsNull()
        );
    }
}