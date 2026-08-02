package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentLink;
import com.knowledgeos.repository.DocumentLinkRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class DocumentLinkBuilderService {

    private final DocumentRepository documentRepository;
    private final DocumentLinkRepository linkRepository;
    private final CrawlerService crawlerService;


    public DocumentLinkBuilderService(
            DocumentRepository documentRepository,
            DocumentLinkRepository linkRepository,
            CrawlerService crawlerService
    ){
        this.documentRepository=documentRepository;
        this.linkRepository=linkRepository;
        this.crawlerService=crawlerService;
    }



    public void buildLinks(){

        List<Document> documents =
                documentRepository.findAll();


        for(Document source : documents){

            List<String> links =
                    crawlerService.extractLinks(source.getUrl());


            for(String url: links){

                documentRepository.findByUrl(url)
                        .ifPresent(target -> {


                            DocumentLink link =
                                    new DocumentLink();

                            link.setSource(source);
                            link.setTarget(target);


                            linkRepository.save(link);

                        });
            }
        }
    }
}