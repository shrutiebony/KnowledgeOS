package com.knowledgeos.model.gnn;

import java.util.List;

public class GnnEmbeddingsResponse {

    private List<Entry> embeddings;

    public List<Entry> getEmbeddings() { return embeddings; }
    public void setEmbeddings(List<Entry> embeddings) { this.embeddings = embeddings; }

    public static class Entry {
        private Long id;
        private List<Float> vector;

        public Long getId() { return id; }
        public void setId(Long id) { this.id = id; }

        public List<Float> getVector() { return vector; }
        public void setVector(List<Float> vector) { this.vector = vector; }
    }
}