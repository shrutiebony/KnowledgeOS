# KnowledgeOS

KnowledgeOS estimates how much of a document collection is likely AI-generated or AI-assisted. You work in a browser: pick a collection, optionally filter by country topic, and read scores, charts, and a document graph. The product is an estimate from writing signals, not proof of authorship, and it does not give medical advice.

You can study built-in encyclopedia and news collections, or add your own material as a separate collection (PDFs, text, or a public website). Extra material is never mixed into the encyclopedia or news collections. You may keep a small number of collections at once. Wikipedia and the news collection cannot be deleted.

Optional explanations can use Claude. When that service is configured, you may ask a question or request a summary grounded in the stored scores and short source excerpts. When it is not configured, the dashboard and its measured readings still work. Claude does not compute the headline AI percentage.

## What you see

- An overall AI-generated share for the visible set, with uncertainty bands (likely AI-generated, likely human-written, or uncertain).
- A yearly view of how that share moves over time when documents have dates.
- A graph of documents clustered by similarity, with collection-level analysis of the same visible set.
- A reading of shared wording among pages that look AI-generated.
- Optional question-and-answer and collection summaries that distinguish what KnowledgeOS measured from what was inferred from excerpts.

## Technology

- A C++20 analysis service with an HTTP API and a browser dashboard.
- SQLite for documents, workflow state, and results.
- A local task scheduler and CPU worker pool: parsing, stylometry, hashed embeddings, calibration, a similarity graph, and GraphSAGE-style neighborhood representations (those representations are not the headline detection score).
- Linux containers (Docker) for a self-contained runtime, with OpenSSL for HTTPS to the open web and, when configured, to Claude.
- Python 3.12 for small helper steps (including optional wording polish when an API key is present).
- External sources: Wikipedia, GDELT news, and sites you ask the product to crawl.

## Run it on your computer

1. Install Docker Desktop (or another Docker Engine that can run Linux containers) and start it.
2. In a terminal, go to the folder you were given for KnowledgeOS.
3. Run:

```
docker compose up --build
```

4. When the container is healthy, open http://localhost:8080 in your browser.

The first start can take several minutes while the Linux image builds. Later starts reuse that image. Collection data is stored in a Docker volume so it survives restarts.

To enable Claude for Ask and collection explanations, set `ANTHROPIC_API_KEY` in the environment Docker uses for this application, then start it again. You may also set `ANTHROPIC_MODEL` if you need a specific Claude model. Leave the key unset if you only need measured scores and the built-in notes.

Stop with `Ctrl+C` in that terminal, or `docker compose down` if you started it in the background.

## One uploaded collection, from add to delete

This is the path of a collection you add yourself (files you upload). It is the same idea on a laptop container and on a hosted deployment: your browser talks to the KnowledgeOS service; the service stores data and computes scores; optional Claude only explains afterward.

1. **Create.** You choose PDF or text files and start ingest. KnowledgeOS opens a new collection for that add. It does not fold those pages into Wikipedia or news.
2. **Extract.** Files are parsed into documents with titles, source names, and text. Each document keeps a stable id so later explanations can point back to it.
3. **Analyze.** A workflow of CPU tasks runs to completion: parse, chunked stylometry and embeddings, calibration against earlier writing in that visible set, graph construction, neighborhood representations, then an aggregate “ready” state. The headline AI share is the mean of document scores from stylometry, local detectors, and embedding distance to a pre-2019 writing centroid—not GraphSAGE.
4. **Inspect.** The dashboard loads that collection. You see the share, bands, time chart when dates exist, the document graph, and the written analysis. You can filter by country topic; that is a view of the same collection, not a second dataset.
5. **Ask (optional).** If Claude is configured, a question or “explain this collection” sends the measured numbers plus a bounded set of excerpts. The reply should keep KnowledgeOS numbers intact and cite those document ids. If Claude is unavailable, you still have the dashboard.
6. **Remove.** When you are finished, you can delete that collection. Its documents and scores go away. Other collections, including Wikipedia and news, stay.

On a deployed host the same lifecycle applies: traffic reaches the container, data lives on the attached disk volume, analysis runs inside that instance, and Claude is reached over the network only after results are stored.
