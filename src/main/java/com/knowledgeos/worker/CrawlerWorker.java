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

import org.springframework.beans.factory.annotation.Value;
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

    private final int maxPagesPerDataset;



    public CrawlerWorker(
            UrlQueueRepository urlQueueRepository,
            CrawlerService crawlerService,
            DocumentRepository documentRepository,
            ChunkingService chunkingService,
            UrlNormalizer urlNormalizer,
            PageLinkRepository pageLinkRepository,
            @Value("${crawler.max-pages-per-dataset:300}") int maxPagesPerDataset
    ) {

        this.urlQueueRepository = urlQueueRepository;
        this.crawlerService = crawlerService;
        this.documentRepository = documentRepository;
        this.chunkingService = chunkingService;
        this.urlNormalizer = urlNormalizer;
        this.pageLinkRepository = pageLinkRepository;
        this.maxPagesPerDataset = maxPagesPerDataset;
    }



    @Scheduled(fixedDelay = 10000)
    public void processQueue() {

        crawlNext();
    }


    public String crawlNext() {
        return crawlNext("default");
    }

    public String crawlNext(String datasetKey) {


        System.out.println("WORKER RUNNING (dataset: " + datasetKey + ")");


        Optional<UrlQueue> item =
                urlQueueRepository.findFirstByVisitedFalseAndDatasetKey(datasetKey);



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



            if(documentRepository.existsByUrlAndDatasetKey(normalizedUrl, datasetKey)) {


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
                    page.getContent(),
                    datasetKey
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



            boolean datasetAtCap =
                    documentRepository.countByDatasetKey(datasetKey) >= maxPagesPerDataset;

            if (datasetAtCap) {
                System.out.println(
                        "Dataset '" + datasetKey + "' has reached its "
                                + maxPagesPerDataset
                                + "-page crawl cap - recording link edges but not queuing further pages."
                );
            }



            for(String link : links) {


                String normalizedLink =
                        urlNormalizer.normalize(link);



                if(!sameHost(normalizedUrl, normalizedLink)) {
                    continue;
                }


                pageLinkRepository.save(new PageLink(savedDocument, normalizedLink));
                if (datasetAtCap) {
                    continue;
                }


                if(!urlQueueRepository.existsByUrlAndDatasetKey(normalizedLink, datasetKey)) {


                    UrlQueue newItem =
                            new UrlQueue();


                    newItem.setUrl(normalizedLink);

                    newItem.setVisited(false);

                    newItem.setDatasetKey(datasetKey);



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