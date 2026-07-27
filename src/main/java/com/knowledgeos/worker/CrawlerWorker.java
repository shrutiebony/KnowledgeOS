package com.knowledgeos.worker;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.UrlQueueRepository;
import com.knowledgeos.service.CrawlerService;
import com.knowledgeos.service.UrlNormalizer;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;
import com.knowledgeos.service.ChunkingService;

import java.io.IOException;
import java.util.List;
import java.util.Optional;



@Component
public class CrawlerWorker {

    private final UrlNormalizer urlNormalizer;
    private final UrlQueueRepository urlQueueRepository;
    private final CrawlerService crawlerService;
    private final DocumentRepository documentRepository;
    private final ChunkingService chunkingService;

    @Value("${crawler.max-pages}")
    private int maxPages;



    public CrawlerWorker(
            UrlNormalizer urlNormalizer, UrlQueueRepository urlQueueRepository,
            CrawlerService crawlerService,
            DocumentRepository documentRepository, ChunkingService chunkingService
    ) {
        this.urlNormalizer = urlNormalizer;

        this.urlQueueRepository = urlQueueRepository;
        this.crawlerService = crawlerService;
        this.documentRepository = documentRepository;
        this.chunkingService = chunkingService;
    }



    @Scheduled(fixedDelay = 2000)
    public void processQueue() throws IOException {


        System.out.println("WORKER RUNNING");


        long documentCount = documentRepository.count();


        if (documentCount >= maxPages) {

            System.out.println(
                    "Crawl limit reached: "
                            + documentCount
                            + "/"
                            + maxPages
            );

            return;
        }



        Optional<UrlQueue> item =
                urlQueueRepository.findFirstByVisitedFalse();



        if (item.isEmpty()) {

            System.out.println("No URLs in queue");

            return;
        }



        UrlQueue queueItem = item.get();



        System.out.println(
                "Crawling: " + queueItem.getUrl()
        );



        String content =
                crawlerService.crawl(queueItem.getUrl());



        Document document = new Document(
                "Crawled Document",
                queueItem.getUrl(),
                content
        );



        Document savedDocument =
                documentRepository.save(document);

        chunkingService.chunk(savedDocument);



        List<String> links =
                crawlerService.extractLinks(queueItem.getUrl());


        System.out.println(
                "Found links: " + links.size()
        );



        for (String link : links) {


            String normalizedUrl =
                    urlNormalizer.normalize(link);


            if (!urlQueueRepository.existsByUrl(normalizedUrl)) {


                UrlQueue newItem = new UrlQueue();

                newItem.setUrl(normalizedUrl);
                newItem.setVisited(false);


                urlQueueRepository.save(newItem);
            }
        }



        queueItem.setVisited(true);


        urlQueueRepository.save(queueItem);



        System.out.println(
                "Finished: " + queueItem.getUrl()
        );
    }
}