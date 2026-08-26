from fastapi import FastAPI
from pydantic import BaseModel
from sentence_transformers import SentenceTransformer

app = FastAPI(title="KnowledgeOS Embedding Service")

# all-MiniLM-L6-v2: 384-dim output, matching the DocumentChunk.embedding
# column (vector(384)) - small (~90MB) and fast enough to run on CPU, no
# GPU or API key required.
model = SentenceTransformer("all-MiniLM-L6-v2")


class EmbedRequest(BaseModel):
    text: str


class EmbedResponse(BaseModel):
    embedding: list[float]


class EmbedBatchRequest(BaseModel):
    texts: list[str]


class EmbedBatchResponse(BaseModel):
    embeddings: list[list[float]]


@app.post("/embed", response_model=EmbedResponse)
def embed(req: EmbedRequest):
    vector = model.encode(req.text, normalize_embeddings=True)
    return EmbedResponse(embedding=vector.tolist())


@app.post("/embed_batch", response_model=EmbedBatchResponse)
def embed_batch(req: EmbedBatchRequest):
    # sentence-transformers batches internally - this is why EmbeddingService
    # prefers generateEmbeddingsBatch() over calling /embed in a loop.
    vectors = model.encode(req.texts, normalize_embeddings=True)
    return EmbedBatchResponse(embeddings=vectors.tolist())


@app.get("/health")
def health():
    return {"status": "ok", "model": "all-MiniLM-L6-v2", "dimensions": 384}