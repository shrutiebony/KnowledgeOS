package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentLink;
import com.knowledgeos.repository.DocumentLinkRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class GraphBuilderService {

    private final DocumentRepository documentRepository;
    private final DocumentLinkRepository documentLinkRepository;
    private final CrawlerService crawlerService;


    public GraphBuilderService(
            DocumentRepository documentRepository,
            DocumentLinkRepository documentLinkRepository,
            CrawlerService crawlerService
    ) {
        this.documentRepository = documentRepository;
        this.documentLinkRepository = documentLinkRepository;
        this.crawlerService = crawlerService;
    }


    public void buildGraph() {

        List<Document> documents = documentRepository.findAll();

        for(Document source : documents){

            List<String> links =
                    crawlerService.extractLinks(source.getUrl());

            for(String link : links){

                Document target =
                        documentRepository.findByUrl(link)
                                .orElse(null);


                if(target != null){

                    DocumentLink documentLink = new DocumentLink();

                    documentLink.setSource(source);
                    documentLink.setTarget(target);

                    documentLinkRepository.save(documentLink);
                }
            }
        }
    }
}