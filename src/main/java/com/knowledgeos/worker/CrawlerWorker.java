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



    /**
     * Pops the next unvisited URL off the queue and runs it through the full
     * crawl -> save -> chunk -> embed pipeline. Public so it can be called
     * both by the scheduler above and on-demand (e.g. from a controller),
     * instead of having two separate, divergent copies of this logic.
     *
     * @return a short human-readable status message describing what happened
     */
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



            /*
             * Skip already crawled documents
             */
            if(documentRepository.existsByUrl(normalizedUrl)) {


                System.out.println(
                        "Already crawled: " + normalizedUrl
                );


                queueItem.setVisited(true);
                urlQueueRepository.save(queueItem);

                return "Already crawled: " + normalizedUrl;
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




            /*
             * Create chunks + embeddings
             */
            chunkingService.chunk(savedDocument);




            /*
             * Discover new links
             */
            List<String> links =
                    crawlerService.extractLinks(normalizedUrl);



            System.out.println(
                    "Found links: " + links.size()
            );




            for(String link : links) {


                String normalizedLink =
                        urlNormalizer.normalize(link);


                // Stay on the same site the crawl started from. Without this,
                // the crawler follows every outbound link on every page and
                // the queue grows unboundedly across the entire web.
                if(!sameHost(normalizedUrl, normalizedLink)) {
                    continue;
                }


                // Persist the hyperlink itself, not just the crawl queue
                // decision - this is the graph HITS/PageRank later run over.
                // Recorded regardless of whether the target is already
                // queued/visited, since the edge exists either way.
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




            /*
             * Mark queue item completed
             */
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