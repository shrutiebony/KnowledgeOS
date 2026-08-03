package com.knowledgeos.controller;

import com.knowledgeos.model.DocumentLink;
import com.knowledgeos.model.RankingScore;
import com.knowledgeos.repository.DocumentLinkRepository;
import com.knowledgeos.repository.RankingScoreRepository;
import com.knowledgeos.service.HitsService;

import org.springframework.web.bind.annotation.*;

import java.util.*;

@RestController
@RequestMapping("/graph")
public class GraphController {

    private final HitsService hitsService;
    private final DocumentLinkRepository documentLinkRepository;
    private final RankingScoreRepository rankingScoreRepository;


    public GraphController(
            HitsService hitsService,
            DocumentLinkRepository documentLinkRepository,
            RankingScoreRepository rankingScoreRepository
    ){
        this.hitsService = hitsService;
        this.documentLinkRepository = documentLinkRepository;
        this.rankingScoreRepository = rankingScoreRepository;
    }



    @GetMapping("/hits")
    public Object hits(){

        Map<Long,List<Long>> graph = new HashMap<>();

        List<DocumentLink> links =
                documentLinkRepository.findAll();



        for(DocumentLink link : links){

            Long source =
                    link.getSource().getId();

            Long target =
                    link.getTarget().getId();


            graph
                    .computeIfAbsent(
                            source,
                            k -> new ArrayList<>()
                    )
                    .add(target);


            graph.putIfAbsent(
                    target,
                    new ArrayList<>()
            );
        }



        Map<Long,double[]> scores =
                hitsService.calculateHits(
                        graph,
                        20
                );



        rankingScoreRepository.deleteAll();



        List<RankingScore> rankingScores =
                new ArrayList<>();


        for(Map.Entry<Long,double[]> entry :
                scores.entrySet()){


            RankingScore score =
                    new RankingScore();


            score.setDocumentId(
                    entry.getKey()
            );


            score.setAuthorityScore(
                    entry.getValue()[0]
            );


            score.setHubScore(
                    entry.getValue()[1]
            );


            rankingScores.add(score);
        }



        rankingScoreRepository.saveAll(
                rankingScores
        );


        return scores;
    }
}