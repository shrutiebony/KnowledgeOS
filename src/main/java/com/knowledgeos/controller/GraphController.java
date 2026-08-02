package com.knowledgeos.controller;

import com.knowledgeos.service.HitsService;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/graph")
public class GraphController {


    private final HitsService hitsService;


    public GraphController(HitsService hitsService){
        this.hitsService=hitsService;
    }



    @GetMapping("/hits")
    public Object hits(){

        Map<Long, List<Long>> graph=new HashMap<>();

        graph.put(1L,List.of(2L,3L));
        graph.put(2L,List.of(3L));
        graph.put(3L,List.of(1L));


        return hitsService.calculateHits(graph,20);
    }
}