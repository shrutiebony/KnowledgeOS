package com.knowledgeos.service;

import com.knowledgeos.model.Dataset;
import com.knowledgeos.model.Document;
import com.knowledgeos.model.DocumentEdge;
import com.knowledgeos.model.GraphSageStatus;
import com.knowledgeos.repository.DatasetRepository;
import com.knowledgeos.repository.DocumentEdgeRepository;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Collectors;

@Service
public class GraphSageService {
    private final DocumentRepository documents;
    private final DocumentEdgeRepository edges;
    private final DatasetRepository datasets;
    private final String python;
    private final String script;

    public GraphSageService(
            DocumentRepository documents,
            DocumentEdgeRepository edges,
            DatasetRepository datasets,
            @Value("${knowledgeos.graphsage.python:python}") String python,
            @Value("${knowledgeos.graphsage.script:tools/graphsage/graphsage_embed.py}") String script) {
        this.documents = documents;
        this.edges = edges;
        this.datasets = datasets;
        this.python = python;
        this.script = script;
    }

    /**
     * Unsupervised neighborhood aggregation. Useful for clusters/unusual patterns.
     * Does not write an AI percentage and does not change headline estimates.
     */
    public void embedUnsupervised(Dataset dataset) {
        List<Document> docs = documents.findByDatasetId(dataset.getId());
        if (docs.size() < 2) {
            dataset.setGraphSageStatus(GraphSageStatus.NOT_TRAINED);
            dataset.setGraphSageMessage("Need at least two documents to build GraphSAGE representations.");
            datasets.save(dataset);
            return;
        }
        try {
            Path dir = Files.createTempDirectory("kos-gs-");
            Path nodes = dir.resolve("nodes.tsv");
            Path edgelist = dir.resolve("edges.tsv");
            Path out = dir.resolve("gnn.tsv");
            StringBuilder nodeSb = new StringBuilder();
            for (Document doc : docs) {
                float[] f = features(doc);
                nodeSb.append(doc.getId());
                for (float v : f) {
                    nodeSb.append('\t').append(v);
                }
                nodeSb.append('\n');
            }
            Files.writeString(nodes, nodeSb.toString(), StandardCharsets.UTF_8);
            String edgeStr = edges.findByDatasetId(dataset.getId()).stream()
                    .map(e -> e.getSourceDocumentId() + "\t" + e.getTargetDocumentId())
                    .collect(Collectors.joining("\n"));
            Files.writeString(edgelist, edgeStr, StandardCharsets.UTF_8);

            Path scriptPath = Path.of(script).toAbsolutePath();
            if (!Files.exists(scriptPath)) {
                meanAggregateFallback(dataset, docs);
                return;
            }
            ProcessBuilder pb = new ProcessBuilder(
                    python, scriptPath.toString(),
                    "--nodes", nodes.toString(),
                    "--edges", edgelist.toString(),
                    "--out", out.toString());
            pb.redirectErrorStream(true);
            Process process = pb.start();
            String log;
            try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                log = br.lines().collect(Collectors.joining("\n"));
            }
            int code = process.waitFor();
            if (code != 0 || !Files.exists(out)) {
                meanAggregateFallback(dataset, docs);
                dataset.setGraphSageMessage("GraphSAGE sidecar unavailable (" + log + "). Used mean-aggregation fallback. Still not a detection signal.");
                datasets.save(dataset);
                return;
            }
            loadEmbeddings(docs, out);
            dataset.setGraphSageStatus(GraphSageStatus.EMBEDDED);
            dataset.setGraphSageMessage("Unsupervised GraphSAGE representations stored. Not used in the headline AI share until a labeled classifier is trained and validated.");
            datasets.save(dataset);
        } catch (Exception e) {
            meanAggregateFallback(dataset, docs);
            dataset.setGraphSageMessage("GraphSAGE sidecar failed (" + e.getMessage() + "). Fallback representations stored. Not a detection signal.");
            datasets.save(dataset);
        }
    }

    public Dataset trainClassifier(Long datasetId, String labelsPath) {
        Dataset dataset = datasets.findById(datasetId).orElseThrow();
        if (labelsPath == null || labelsPath.isBlank() || !Files.exists(Path.of(labelsPath))) {
            dataset.setGraphSageStatus(GraphSageStatus.NOT_TRAINED);
            dataset.setGraphSageMessage("Labeled human/AI examples are required before GraphSAGE can be treated as a detection signal. Headline estimates still use calibrated stylometry, detectors, embedding anomalies, and baseline deviation only.");
            return datasets.save(dataset);
        }
        dataset.setGraphSageStatus(GraphSageStatus.VALIDATION_FAILED);
        dataset.setGraphSageMessage("A labels file was provided, but v1 does not promote GraphSAGE into the headline until hold-out validation is implemented and passing. Representations may still be used for graph exploration.");
        return datasets.save(dataset);
    }

    private void meanAggregateFallback(Dataset dataset, List<Document> docs) {
        List<DocumentEdge> datasetEdges = edges.findByDatasetId(dataset.getId());
        for (Document doc : docs) {
            float[] self = features(doc);
            float[] acc = new float[self.length];
            System.arraycopy(self, 0, acc, 0, self.length);
            int n = 1;
            for (DocumentEdge edge : datasetEdges) {
                if (!edge.getSourceDocumentId().equals(doc.getId())) {
                    continue;
                }
                documents.findById(edge.getTargetDocumentId()).ifPresent(nb -> {
                    float[] f = features(nb);
                    for (int i = 0; i < acc.length && i < f.length; i++) {
                        acc[i] += f[i];
                    }
                });
                n++;
            }
            float inv = 1.0f / n;
            for (int i = 0; i < acc.length; i++) {
                acc[i] *= inv;
            }
            doc.setGnnEmbedding(acc);
        }
        documents.saveAll(docs);
        dataset.setGraphSageStatus(GraphSageStatus.EMBEDDED);
        dataset.setGraphSageMessage("Mean-aggregation GraphSAGE-style representations stored. Not used in the headline AI share.");
        datasets.save(dataset);
    }

    private void loadEmbeddings(List<Document> docs, Path out) throws Exception {
        List<String> lines = Files.readAllLines(out);
        for (String line : lines) {
            if (line.isBlank()) {
                continue;
            }
            String[] p = line.split("\\t");
            long id = Long.parseLong(p[0]);
            float[] v = new float[p.length - 1];
            for (int i = 1; i < p.length; i++) {
                v[i - 1] = Float.parseFloat(p[i]);
            }
            for (Document doc : docs) {
                if (doc.getId() == id) {
                    doc.setGnnEmbedding(v);
                }
            }
        }
        documents.saveAll(docs);
    }

    private static float[] features(Document doc) {
        return new float[]{
                f(doc.getDetectorStylometry()),
                f(doc.getDetectorRepetition()),
                f(doc.getDetectorUniformity()),
                f(doc.getEmbeddingAnomaly()),
                f(doc.getStylometryDeviation()),
                f(doc.getTypeTokenRatio()),
                f(doc.getBurstiness()),
                f(doc.getPAi())
        };
    }

    private static float f(Double v) {
        return v == null ? 0f : v.floatValue();
    }
}
