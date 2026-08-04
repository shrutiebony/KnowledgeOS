package com.knowledgeos.worker;


import com.knowledgeos.model.CrawledPage;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.PageLink;
import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.DocumentRepository;
import com.knowledgeos.repository.PageLinkRepository;
import com.knowledgeos.repository.UrlQueueRepository;
import com.knowledgeos.service.ChunkingService;
import com.knowledgeos.service.CrawlerService;
import com.knowledgeos.service.UrlNormalizer;

import org.springframework.dao.DataIntegrityViolationException;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.net.URI;
import java.util.List;
import java.util.Optional;


@Component
public class CrawlerWorker {


    private final UrlQueueRepository urlQueueRepository;

    private final CrawlerService crawlerService;

    private final DocumentRepository documentRepository;

    private final ChunkingService chunkingService;

    private final UrlNormalizer urlNormalizer;

    private final PageLinkRepository pageLinkRepository;



    public CrawlerWorker(
            UrlQueueRepository urlQueueRepository,
            CrawlerService crawlerService,
            DocumentRepository documentRepository,
            ChunkingService chunkingService,
            UrlNormalizer urlNormalizer,
            PageLinkRepository pageLinkRepository
    ) {

        this.urlQueueRepository = urlQueueRepository;
        this.crawlerService = crawlerService;
        this.documentRepository = documentRepository;
        this.chunkingService = chunkingService;
        this.urlNormalizer = urlNormalizer;
        this.pageLinkRepository = pageLinkRepository;
    }



    @Scheduled(fixedDelay = 10000)
    public void processQueue() {

        crawlNext();
    }



    public String crawlNext() {


        System.out.println("WORKER RUNNING");


        Optional<UrlQueue> item =
                urlQueueRepository.findFirstByVisitedFalse();



        if(item.isEmpty()) {
            return "Queue empty";
        }



        UrlQueue queueItem = item.get();


        String normalizedUrl =
                urlNormalizer.normalize(queueItem.getUrl());


        try {


            System.out.println(
                    "Crawling: " + normalizedUrl
            );



            if(documentRepository.existsByUrl(normalizedUrl)) {


                System.out.println(
                        "Already crawled: " + normalizedUrl
                );


                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return "Already crawled: " + normalizedUrl;
            }



            CrawledPage page =
                    crawlerService.crawl(normalizedUrl);



            if(page.getContent() == null ||
                    page.getContent().isEmpty()) {


                System.out.println(
                        "Empty content: " + normalizedUrl
                );


                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return "Empty content: " + normalizedUrl;
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

                return "Duplicate document skipped: " + normalizedUrl;
            }




            chunkingService.chunk(savedDocument);




            List<String> links =
                    crawlerService.extractLinks(normalizedUrl);



            System.out.println(
                    "Found links: " + links.size()
            );




            for(String link : links) {


                String normalizedLink =
                        urlNormalizer.normalize(link);


                if(!sameHost(normalizedUrl, normalizedLink)) {
                    continue;
                }


                pageLinkRepository.save(new PageLink(savedDocument, normalizedLink));



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




            queueItem.setUrl(normalizedUrl);

            queueItem.setVisited(true);


            urlQueueRepository.save(queueItem);



            System.out.println(
                    "Finished: " + normalizedUrl
            );

            return "Crawled: " + normalizedUrl;



        } catch(Exception e) {


            System.out.println(
                    "Crawler failed: " + normalizedUrl
            );


            e.printStackTrace();



            queueItem.setVisited(true);

            urlQueueRepository.save(queueItem);

            return "Crawler failed: " + normalizedUrl;
        }
    }



    private boolean sameHost(String urlA, String urlB) {

        try {
            String hostA = new URI(urlA).getHost();
            String hostB = new URI(urlB).getHost();

            return hostA != null && hostA.equalsIgnoreCase(hostB);

        } catch (Exception e) {
            return false;
        }
    }
}