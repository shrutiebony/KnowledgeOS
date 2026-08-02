package com.knowledgeos.worker;


import com.knowledgeos.model.CrawledPage;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.UrlQueueRepository;
import com.knowledgeos.service.ChunkingService;
import com.knowledgeos.service.CrawlerService;
import com.knowledgeos.service.UrlNormalizer;

import org.springframework.dao.DataIntegrityViolationException;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.List;
import java.util.Optional;


@Component
public class CrawlerWorker {


    private final UrlQueueRepository urlQueueRepository;

    private final CrawlerService crawlerService;

    private final DocumentRepository documentRepository;

    private final ChunkingService chunkingService;

    private final UrlNormalizer urlNormalizer;



    public CrawlerWorker(
            UrlQueueRepository urlQueueRepository,
            CrawlerService crawlerService,
            DocumentRepository documentRepository,
            ChunkingService chunkingService,
            UrlNormalizer urlNormalizer
    ) {

        this.urlQueueRepository = urlQueueRepository;
        this.crawlerService = crawlerService;
        this.documentRepository = documentRepository;
        this.chunkingService = chunkingService;
        this.urlNormalizer = urlNormalizer;
    }



    @Scheduled(fixedDelay = 10000)
    public void processQueue() {




        Optional<UrlQueue> item =
                urlQueueRepository.findFirstByVisitedFalse();



        if(item.isEmpty()) {
            return;
        }



        UrlQueue queueItem = item.get();


        String normalizedUrl =
                urlNormalizer.normalize(queueItem.getUrl());


        try {


            System.out.println(
                    "Crawling: " + normalizedUrl
            );



            /*
             * Skip already crawled documents
             */
            if(documentRepository.existsByUrl(normalizedUrl)) {


                System.out.println(
                        "Already crawled: " + normalizedUrl
                );


                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return;
            }



            /*
             * Crawl webpage
             */
            CrawledPage page =
                    crawlerService.crawl(normalizedUrl);



            if(page.getContent() == null ||
                    page.getContent().isEmpty()) {


                System.out.println(
                        "Empty content: " + normalizedUrl
                );


                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return;
            }




            Document document = new Document(
                    page.getTitle(),
                    normalizedUrl,
                    page.getContent()
            );


            Document savedDocument;

            try {

                savedDocument = documentRepository.save(document);

            } catch (DataIntegrityViolationException e) {

                System.out.println(
                        "Duplicate document skipped: "
                                + normalizedUrl
                );

                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return;
            }




            /*
             * Create chunks + embeddings
             */
            chunkingService.chunk(savedDocument);




            /*
             * Discover new links
             */
            List<String> links =
                    crawlerService.extractLinks(normalizedUrl);







            for(String link : links) {


                String normalizedLink =
                        urlNormalizer.normalize(link);



                if(!urlQueueRepository.existsByUrl(normalizedLink)) {


                    UrlQueue newItem =
                            new UrlQueue();


                    newItem.setUrl(normalizedLink);

                    newItem.setVisited(false);



                    try {

                        urlQueueRepository.save(newItem);


                    } catch(DataIntegrityViolationException e) {


                        System.out.println(
                                "Duplicate queue URL skipped: "
                                        + normalizedLink
                        );
                    }
                }
            }




            /*
             * Mark queue item completed
             */
            queueItem.setUrl(normalizedUrl);

            queueItem.setVisited(true);


            urlQueueRepository.save(queueItem);







        } catch(Exception e) {


            System.out.println(
                    "Crawler failed: " + normalizedUrl
            );


            e.printStackTrace();



            queueItem.setVisited(true);

            urlQueueRepository.save(queueItem);
        }
    }
}