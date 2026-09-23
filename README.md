# KnowledgeOS

KnowledgeOS estimates how much of a **document collection** is likely AI-generated or AI-assisted. It shows a document similarity graph and examples. It does **not** answer questions about article content, make medical judgments, or claim to prove authorship.

HITS and general Q&A are out of scope.

## What you get

| View | Meaning |
|---|---|
| Overall estimate | Estimated AI-generated share, with an uncertainty range and document count. Toggle **share of documents** vs **share of analyzed words**. |
| Document graph | Documents as nodes; edges from embedding, topic, or source similarity; color by likely AI / likely human / uncertain. |
| Examples | Clickable documents from across the collection, not only the extreme tail. |
| Breakdown | By source, topic, or time when metadata exists. |
| Explanation | Signals and comparable neighbors behind one estimate. |

GraphSAGE learns a neighborhood representation for each document. That representation is **not** part of the headline percentage until a labeled classifier is trained and validated.

## Ingest

1. Use the stored **Wikipedia India** corpus (crawled from English Wikipedia on first run). A Wikipedia topic filters that same collection — it is not a second dataset.
2. **Upload PDFs** (also `.txt`, `.md`, `.html`, `.jsonl`, `.zip`).
3. **Provide public URLs**. Each seed is fetched, then same-host child pages are crawled (depth 2, up to 80 pages) into one independent `USER_URLS` dataset.

Each dataset is stored and analyzed independently. URL collections are never mixed into the Wikipedia India graph; they use the same analysis methods on their own documents.

## Run

Requires Java 21 and Maven.

```bash
mvn spring-boot:run
```

Open http://localhost:8080

A second independent collection is in `data/examples/medical/` (upload those files).

Optional local embedding HTTP service: set `knowledgeos.embed.url` (expects `POST /embed_batch`). If unset, hashed embeddings are used.

GraphSAGE sidecar: `tools/graphsage/graphsage_embed.py`. If Python is missing, Java mean-aggregation is used. Neither path writes the dashboard headline.

## Path

documents → graph + detection signals → calibrated document estimates → dataset percentage and examples.

Detection signals (headline): stylometry, multiple local detectors, embedding anomaly vs the dataset’s own centroid, statistical deviation from that dataset’s stylometry baseline. Combined with a logistic blend and disagreement-based interval.
