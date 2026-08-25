package com.knowledgeos.controller;


import com.knowledgeos.model.Document;
import com.knowledgeos.model.StatusResponse;
import com.knowledgeos.repository.DocumentChunkRepository;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.UrlQueueRepository;
import com.knowledgeos.service.DatasetService;
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

    private final DatasetService datasetService;



    public DocumentController(
            DocumentRepository documentRepository,
            DocumentChunkRepository documentChunkRepository,
            UrlQueueRepository urlQueueRepository,
            UrlQueueService urlQueueService,
            CrawlerWorker crawlerWorker,
            DatasetService datasetService
    ) {
        this.documentRepository = documentRepository;
        this.documentChunkRepository = documentChunkRepository;
        this.urlQueueRepository = urlQueueRepository;
        this.urlQueueService = urlQueueService;
        this.crawlerWorker = crawlerWorker;
        this.datasetService = datasetService;
    }



    @GetMapping("/documents")
    public List<Document> getDocuments(
            @RequestParam(required = false) String datasetKey
    ){

        return datasetKey == null
                ? documentRepository.findAll()
                : documentRepository.findByDatasetKey(datasetKey);
    }



    // Every distinct corpus currently in Postgres - "default" (the free-form
    // crawl), any static snapshots (e.g. "wikipedia-india"), and any other
    // dynamic crawl session keys a user has created.
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



    // datasetKey lets a user run several independent "crawl anything"
    // sessions side by side (e.g. datasetKey=my-research-crawl) without
    // their queues, HITS scores, or chat answers bleeding into each other.
    // Omit it to use the original single free-form "default" crawl.
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

        // Delegates to the same pipeline the scheduled worker uses, instead
        // of a second copy that skipped chunking/embedding and never
        // persisted the visited flag.
        return crawlerWorker.crawlNext(datasetKey);
    }



    // Wipes every row tagged with this datasetKey: documents, chunks, page
    // links, entities, relationships, GNN embeddings, predicted links, and
    // any pending/visited queue entries. Irreversible - there's no undo
    // short of a database backup.
    @DeleteMapping("/datasets/{datasetKey}")
    public String deleteDataset(
            @PathVariable String datasetKey
    ){

        datasetService.deleteDataset(datasetKey);

        return "Deleted dataset '" + datasetKey + "'";
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