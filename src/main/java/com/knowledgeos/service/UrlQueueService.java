package com.knowledgeos.service;

import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.UrlQueueRepository;
import org.springframework.stereotype.Service;


@Service
public class UrlQueueService {


    private final UrlQueueRepository urlQueueRepository;

    private final UrlNormalizer urlNormalizer;



    public UrlQueueService(
            UrlQueueRepository urlQueueRepository,
            UrlNormalizer urlNormalizer
    ) {

        this.urlQueueRepository = urlQueueRepository;
        this.urlNormalizer = urlNormalizer;

    }



    public void addUrl(String url) {
        addUrl(url, "default");
    }

    public void addUrl(String url, String datasetKey) {


        String normalizedUrl =
                urlNormalizer.normalize(url);



        if(urlQueueRepository.existsByUrlAndDatasetKey(normalizedUrl, datasetKey)) {

            System.out.println(
                    "URL already exists in dataset " + datasetKey + ": " + normalizedUrl
            );

            return;
        }



        UrlQueue item = new UrlQueue();


        item.setUrl(normalizedUrl);

        item.setVisited(false);

        item.setDatasetKey(datasetKey);


        urlQueueRepository.save(item);
    }



    public UrlQueue getNextUrl() {

        return urlQueueRepository
                .findFirstByVisitedFalse()
                .orElse(null);
    }



    public long size() {

        return urlQueueRepository.count();

    }
}