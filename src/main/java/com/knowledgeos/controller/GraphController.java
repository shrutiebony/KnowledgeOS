package com.knowledgeos.controller;

import com.knowledgeos.model.GraphEntity;
import com.knowledgeos.model.GraphEdge;
import com.knowledgeos.model.GraphNode;
import com.knowledgeos.model.GraphResponse;
import com.knowledgeos.model.Relationship;
import com.knowledgeos.repository.GraphEntityRepository;
import com.knowledgeos.repository.RelationshipRepository;
import org.springframework.web.bind.annotation.CrossOrigin;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController

@CrossOrigin(origins = "*")
public class GraphController {

    private final GraphEntityRepository entityRepository;
    private final RelationshipRepository relationshipRepository;

    public GraphController(
            GraphEntityRepository entityRepository,
            RelationshipRepository relationshipRepository
    ) {
        this.entityRepository = entityRepository;
        this.relationshipRepository = relationshipRepository;
    }

    @GetMapping("/graph")
    public GraphResponse graph(
            @org.springframework.web.bind.annotation.RequestParam(defaultValue = "default") String datasetKey
    ) {


        List<GraphEntity> entities =
                entityRepository.findTop150ByDatasetKeyOrderByFrequencyDesc(datasetKey);

        List<Long> ids = entities.stream().map(GraphEntity::getId).toList();

        List<Relationship> relationships =
                relationshipRepository.findBySource_IdInAndTarget_IdIn(ids, ids);

        List<GraphNode> nodes = entities.stream()
                .map(e -> new GraphNode(e.getId(), e.getName(), e.getType(), e.getFrequency()))
                .toList();

        List<GraphEdge> edges = relationships.stream()
                .map(r -> new GraphEdge(
                        r.getSource().getId(),
                        r.getTarget().getId(),
                        r.getRelationType()
                ))
                .toList();

        return new GraphResponse(nodes, edges);
    }
}