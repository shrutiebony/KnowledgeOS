package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.stereotype.Service;

import java.util.List;


@Service
public class DocumentService {


    private final DocumentRepository documentRepository;


    public DocumentService(DocumentRepository documentRepository){

        this.documentRepository = documentRepository;

    }


    public void save(Document document){

        documentRepository.save(document);

    }


    public List<Document> getDocuments(){

        return documentRepository.findAll();

    }

}