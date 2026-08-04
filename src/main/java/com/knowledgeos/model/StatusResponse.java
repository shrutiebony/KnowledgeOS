package com.knowledgeos.model;

public class StatusResponse {

    private final long documentsIndexed;
    private final long queuePending;
    private final long queueVisited;
    private final long chunksTotal;
    private final long chunksMissingEmbedding;

    public StatusResponse(
            long documentsIndexed,
            long queuePending,
            long queueVisited,
            long chunksTotal,
            long chunksMissingEmbedding
    ) {
        this.documentsIndexed = documentsIndexed;
        this.queuePending = queuePending;
        this.queueVisited = queueVisited;
        this.chunksTotal = chunksTotal;
        this.chunksMissingEmbedding = chunksMissingEmbedding;
    }

    public long getDocumentsIndexed() {
        return documentsIndexed;
    }

    public long getQueuePending() {
        return queuePending;
    }

    public long getQueueVisited() {
        return queueVisited;
    }

    public long getChunksTotal() {
        return chunksTotal;
    }

    public long getChunksMissingEmbedding() {
        return chunksMissingEmbedding;
    }
}
