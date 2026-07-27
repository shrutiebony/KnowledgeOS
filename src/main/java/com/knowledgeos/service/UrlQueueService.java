package com.knowledgeos.service;

import com.knowledgeos.model.UrlQueue;
import com.knowledgeos.repository.UrlQueueRepository;
import org.springframework.stereotype.Service;


@Service
public class UrlQueueService {


    private final UrlQueueRepository urlQueueRepository;


    public UrlQueueService(UrlQueueRepository urlQueueRepository) {
        this.urlQueueRepository = urlQueueRepository;
    }


    public void addUrl(String url) {

        UrlQueue item = new UrlQueue();

        item.setUrl(url);
        item.setVisited(false);

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