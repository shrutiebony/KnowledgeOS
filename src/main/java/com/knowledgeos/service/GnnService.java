package com.knowledgeos.service;

import com.knowledgeos.model.GnnEmbedding;
import com.knowledgeos.model.GraphEntity;
import com.knowledgeos.model.PredictedLink;
import com.knowledgeos.model.Relationship;
import com.knowledgeos.model.gnn.*;
import com.knowledgeos.repository.GnnEmbeddingRepository;
import com.knowledgeos.repository.GraphEntityRepository;
import com.knowledgeos.repository.PredictedLinkRepository;
import com.knowledgeos.repository.RelationshipRepository;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import java.net.http.HttpClient;
import java.time.Duration;
import java.util.List;
import java.util.Map;

@Service
public class GnnService {

    private final GraphEntityRepository entityRepository;
    private final RelationshipRepository relationshipRepository;
    private final GnnEmbeddingRepository gnnEmbeddingRepository;
    private final PredictedLinkRepository predictedLinkRepository;
    private final RestClient restClient;
    private final String gnnServiceBaseUrl;

    public GnnService(
            GraphEntityRepository entityRepository,
            RelationshipRepository relationshipRepository,
            GnnEmbeddingRepository gnnEmbeddingRepository,
            PredictedLinkRepository predictedLinkRepository,
            @Value("${gnn.service.url:http://localhost:8001}") String gnnServiceBaseUrl
    ) {
        this.entityRepository = entityRepository;
        this.relationshipRepository = relationshipRepository;
        this.gnnEmbeddingRepository = gnnEmbeddingRepository;
        this.predictedLinkRepository = predictedLinkRepository;
        this.gnnServiceBaseUrl = gnnServiceBaseUrl;

        HttpClient httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .build();

        JdkClientHttpRequestFactory factory = new JdkClientHttpRequestFactory(httpClient);
        factory.setReadTimeout(Duration.ofSeconds(180));

        this.restClient = RestClient.builder()
                .requestFactory(factory)
                .build();
    }

    public Map<String, Object> trainAndRefresh(int topKPredictedLinks) {
        return trainAndRefresh(topKPredictedLinks, "default");
    }

    public Map<String, Object> trainAndRefresh(int topKPredictedLinks, String datasetKey) {

        List<GraphEntity> entities = entityRepository.findByDatasetKey(datasetKey);
        List<Long> entityIds = entities.stream().map(GraphEntity::getId).toList();
        List<Relationship> relationships =
                relationshipRepository.findBySource_IdInAndTarget_IdIn(entityIds, entityIds);

        if (entities.size() < 2 || relationships.isEmpty()) {
            throw new IllegalStateException(
                    "Not enough graph data to train yet - need at least 2 entities and 1 relationship. " +
                            "Run entity extraction first."
            );
        }

        List<GnnNodeDto> nodes = entities.stream()
                .map(e -> new GnnNodeDto(e.getId(), e.getType(), e.getFrequency()))
                .toList();

        List<GnnEdgeDto> edges = relationships.stream()
                .map(r -> new GnnEdgeDto(r.getSource().getId(), r.getTarget().getId()))
                .toList();

        GnnTrainResponse trainResponse = restClient.post()
                .uri(gnnServiceBaseUrl + "/gnn/train")
                .body(new GnnTrainRequest(nodes, edges))
                .retrieve()
                .body(GnnTrainResponse.class);

        persistEmbeddings();
        persistPredictedLinks(topKPredictedLinks, datasetKey);

        assert trainResponse != null;
        return Map.of(
                "nodesTrained", trainResponse.getNodesTrained(),
                "edgesTrained", trainResponse.getEdgesTrained(),
                "epochs", trainResponse.getEpochs(),
                "finalLoss", trainResponse.getFinalLoss(),
                "embeddingDim", trainResponse.getEmbeddingDim()
        );
    }

    private void persistEmbeddings() {

        GnnEmbeddingsResponse response = restClient.get()
                .uri(gnnServiceBaseUrl + "/gnn/embeddings")
                .retrieve()
                .body(GnnEmbeddingsResponse.class);

        assert response != null;

        for (GnnEmbeddingsResponse.Entry entry : response.getEmbeddings()) {

            GnnEmbedding row = gnnEmbeddingRepository.findByEntityId(entry.getId())
                    .orElseGet(GnnEmbedding::new);

            row.setEntityId(entry.getId());

            float[] vector = new float[entry.getVector().size()];
            for (int i = 0; i < vector.length; i++) {
                vector[i] = entry.getVector().get(i);
            }
            row.setEmbedding(vector);

            gnnEmbeddingRepository.save(row);
        }
    }

    private void persistPredictedLinks(int topK, String datasetKey) {

        GnnLinkPredictionResponse response = restClient.post()
                .uri(gnnServiceBaseUrl + "/gnn/predict-links")
                .body(new GnnLinkPredictionRequest(topK, null))
                .retrieve()
                .body(GnnLinkPredictionResponse.class);

        assert response != null;

        predictedLinkRepository.deleteBySource_DatasetKey(datasetKey);

        for (GnnLinkPredictionResponse.Prediction p : response.getPredictions()) {

            GraphEntity source = entityRepository.findById(p.getSource()).orElse(null);
            GraphEntity target = entityRepository.findById(p.getTarget()).orElse(null);

            if (source == null || target == null) {
                continue;
            }

            predictedLinkRepository.save(new PredictedLink(source, target, p.getScore()));
        }
    }
    public List<GnnEmbedding> findSimilarEntities(Long entityId, int limit) {

        GnnEmbedding target = gnnEmbeddingRepository.findByEntityId(entityId)
                .orElseThrow(() -> new IllegalStateException(
                        "No GNN embedding for entity " + entityId + " - train the model first"));

        return gnnEmbeddingRepository.searchSimilar(
                convert(target.getEmbedding()), entityId, limit);
    }

    private String convert(float[] vector) {

        StringBuilder sb = new StringBuilder("[");

        for (int i = 0; i < vector.length; i++) {
            sb.append(vector[i]);
            if (i < vector.length - 1) {
                sb.append(",");
            }
        }

        sb.append("]");

        return sb.toString();
    }
}