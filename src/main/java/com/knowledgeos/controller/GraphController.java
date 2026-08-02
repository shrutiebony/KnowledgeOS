package com.knowledgeos.controller;

import com.knowledgeos.model.DocumentLink;
import com.knowledgeos.repository.DocumentLinkRepository;
import com.knowledgeos.service.HitsService;

import org.springframework.web.bind.annotation.*;

import java.util.*;

@RestController
@RequestMapping("/graph")
public class GraphController {

    private final HitsService hitsService;
    private final DocumentLinkRepository documentLinkRepository;


    public GraphController(
            HitsService hitsService,
            DocumentLinkRepository documentLinkRepository
    ){
        this.hitsService = hitsService;
        this.documentLinkRepository = documentLinkRepository;
    }


    @GetMapping("/hits")
    public Object hits(){

        Map<Long, List<Long>> graph = new HashMap<>();

        List<DocumentLink> links = documentLinkRepository.findAll();


        for(DocumentLink link : links){

            Long source = link.getSource().getId();
            Long target = link.getTarget().getId();

            graph
                    .computeIfAbsent(source, k -> new ArrayList<>())
                    .add(target);

            graph.putIfAbsent(target, new ArrayList<>());
        }


        return hitsService.calculateHits(graph, 20);
    }
}