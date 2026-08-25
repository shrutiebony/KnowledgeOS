"""
KnowledgeOS GNN Service
------------------------
Sidecar service (mirrors the existing embedding server pattern at
:8000/embed) that fits a GraphSAGE encoder over the entity/relationship
knowledge graph and serves:

  POST /gnn/train           - (re)train on the current graph snapshot
  GET  /gnn/embeddings      - node embeddings from the last training run
  POST /gnn/predict-links   - top-K scored candidate edges not yet in the graph

The graph is small enough (entities, not raw web pages) that keeping the
trained model + embeddings in process memory is fine for a first version -
call /gnn/train again any time the underlying graph changes materially
(e.g. after EntityExtractionScheduler runs). There's no persistence here on
purpose; the Java side is the system of record and writes results back to
Postgres after each call.
"""

from typing import List, Optional

import torch
import torch.nn.functional as F
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from torch_geometric.nn import SAGEConv
from torch_geometric.utils import negative_sampling

app = FastAPI(title="KnowledgeOS GNN Service")

ENTITY_TYPES = ["PERSON", "ORGANIZATION", "TECHNOLOGY", "OTHER"]
EMBEDDING_DIM = 64
HIDDEN_DIM = 128
EPOCHS = 200
LEARNING_RATE = 0.01


# ---------------------------------------------------------------------------
# Request / response schemas
# ---------------------------------------------------------------------------

class NodeIn(BaseModel):
    id: int
    type: str
    frequency: int = 1


class EdgeIn(BaseModel):
    source: int
    target: int


class TrainRequest(BaseModel):
    nodes: List[NodeIn]
    edges: List[EdgeIn]


class TrainResponse(BaseModel):
    nodesTrained: int
    edgesTrained: int
    epochs: int
    finalLoss: float
    embeddingDim: int


class EmbeddingOut(BaseModel):
    id: int
    vector: List[float]


class EmbeddingsResponse(BaseModel):
    embeddings: List[EmbeddingOut]


class LinkPredictionRequest(BaseModel):
    topK: int = 20
    # Optional: restrict predictions to candidates involving these node ids
    # (e.g. one entity's "possible connections"). Omit for a graph-wide scan.
    nodeIds: Optional[List[int]] = None


class LinkPredictionOut(BaseModel):
    source: int
    target: int
    score: float


class LinkPredictionResponse(BaseModel):
    predictions: List[LinkPredictionOut]


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

class GraphSAGEEncoder(torch.nn.Module):
    def __init__(self, in_dim: int, hidden_dim: int, out_dim: int):
        super().__init__()
        self.conv1 = SAGEConv(in_dim, hidden_dim)
        self.conv2 = SAGEConv(hidden_dim, out_dim)

    def forward(self, x, edge_index):
        h = self.conv1(x, edge_index)
        h = F.relu(h)
        h = F.dropout(h, p=0.2, training=self.training)
        h = self.conv2(h, edge_index)
        return h


def decode_score(z, edge_index):
    """Dot-product link score between node pairs, as raw logits."""
    src, dst = edge_index
    return (z[src] * z[dst]).sum(dim=-1)


# ---------------------------------------------------------------------------
# In-memory trained state
# ---------------------------------------------------------------------------

class TrainedState:
    def __init__(self):
        self.model: Optional[GraphSAGEEncoder] = None
        self.embeddings: Optional[torch.Tensor] = None
        self.id_to_index: dict = {}
        self.index_to_id: dict = {}
        self.existing_edges: set = set()
        self.num_nodes: int = 0

    def is_trained(self) -> bool:
        return self.embeddings is not None


state = TrainedState()


def build_features(nodes: List[NodeIn]) -> torch.Tensor:
    """One-hot entity type + log-scaled frequency as node features."""
    rows = []
    for n in nodes:
        one_hot = [1.0 if n.type == t else 0.0 for t in ENTITY_TYPES]
        freq_feature = [float(torch.log1p(torch.tensor(float(max(n.frequency, 0)))))]
        rows.append(one_hot + freq_feature)
    return torch.tensor(rows, dtype=torch.float)


@app.post("/gnn/train", response_model=TrainResponse)
def train(req: TrainRequest):
    if len(req.nodes) < 2:
        raise HTTPException(status_code=400, detail="Need at least 2 nodes to train")

    id_to_index = {n.id: i for i, n in enumerate(req.nodes)}
    index_to_id = {i: n.id for i, n in enumerate(req.nodes)}

    edge_pairs = []
    existing_edges = set()
    for e in req.edges:
        if e.source not in id_to_index or e.target not in id_to_index:
            continue
        s, t = id_to_index[e.source], id_to_index[e.target]
        if s == t:
            continue
        edge_pairs.append((s, t))
        edge_pairs.append((t, s))  # treat as undirected for message passing
        existing_edges.add((e.source, e.target))
        existing_edges.add((e.target, e.source))

    if len(edge_pairs) == 0:
        raise HTTPException(status_code=400, detail="Need at least 1 edge to train")

    x = build_features(req.nodes)
    edge_index = torch.tensor(edge_pairs, dtype=torch.long).t().contiguous()

    in_dim = x.size(1)
    model = GraphSAGEEncoder(in_dim, HIDDEN_DIM, EMBEDDING_DIM)
    optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)

    num_nodes = len(req.nodes)
    model.train()
    final_loss = 0.0

    for epoch in range(EPOCHS):
        optimizer.zero_grad()

        z = model(x, edge_index)

        pos_edge_index = edge_index
        neg_edge_index = negative_sampling(
            edge_index=edge_index,
            num_nodes=num_nodes,
            num_neg_samples=pos_edge_index.size(1),
        )

        pos_score = decode_score(z, pos_edge_index)
        neg_score = decode_score(z, neg_edge_index)

        scores = torch.cat([pos_score, neg_score])
        labels = torch.cat([
            torch.ones(pos_score.size(0)),
            torch.zeros(neg_score.size(0)),
        ])

        loss = F.binary_cross_entropy_with_logits(scores, labels)
        loss.backward()
        optimizer.step()
        final_loss = float(loss.item())

    model.eval()
    with torch.no_grad():
        final_embeddings = model(x, edge_index)

    state.model = model
    state.embeddings = final_embeddings
    state.id_to_index = id_to_index
    state.index_to_id = index_to_id
    state.existing_edges = existing_edges
    state.num_nodes = num_nodes

    return TrainResponse(
        nodesTrained=num_nodes,
        edgesTrained=len(req.edges),
        epochs=EPOCHS,
        finalLoss=final_loss,
        embeddingDim=EMBEDDING_DIM,
    )


@app.get("/gnn/embeddings", response_model=EmbeddingsResponse)
def embeddings():
    if not state.is_trained():
        raise HTTPException(status_code=409, detail="Model not trained yet - call /gnn/train first")

    out = []
    for idx in range(state.num_nodes):
        node_id = state.index_to_id[idx]
        vector = state.embeddings[idx].tolist()
        out.append(EmbeddingOut(id=node_id, vector=vector))

    return EmbeddingsResponse(embeddings=out)


@app.post("/gnn/predict-links", response_model=LinkPredictionResponse)
def predict_links(req: LinkPredictionRequest):
    if not state.is_trained():
        raise HTTPException(status_code=409, detail="Model not trained yet - call /gnn/train first")

    z = state.embeddings
    n = state.num_nodes

    candidate_indices = (
        [state.id_to_index[nid] for nid in req.nodeIds if nid in state.id_to_index]
        if req.nodeIds
        else list(range(n))
    )

    scored = []
    with torch.no_grad():
        for i in candidate_indices:
            # Score i against all other nodes in one shot.
            zi = z[i].unsqueeze(0).expand(n, -1)
            scores = (zi * z).sum(dim=-1)
            probs = torch.sigmoid(scores)

            for j in range(n):
                if i == j:
                    continue
                src_id = state.index_to_id[i]
                dst_id = state.index_to_id[j]
                if (src_id, dst_id) in state.existing_edges:
                    continue
                # Avoid double-counting the same undirected pair twice
                # when scanning the whole graph.
                if req.nodeIds is None and src_id > dst_id:
                    continue
                scored.append((src_id, dst_id, float(probs[j])))

    scored.sort(key=lambda t: t[2], reverse=True)
    top = scored[: req.topK]

    return LinkPredictionResponse(
        predictions=[
            LinkPredictionOut(source=s, target=t, score=sc) for s, t, sc in top
        ]
    )


@app.get("/health")
def health():
    return {"status": "ok", "trained": state.is_trained()}