# KnowledgeOS

Collection-level estimate of how much of a document set looks AI-generated or AI-assisted.

## What it offers

A dashboard for researchers, editors, and analysts who need a collection-level read on AI-looking prose or topic or country-wise analysis of open source data. Current sources include: Wikipedia and GDELT as public baselines, plus PDFs or open-source public URL the user would like to give.

You can:

- Current use case: Compare how much of a corpus looks AI-generated or AI-assisted, by country topic and by source ( to be extended later )
- Add your own files or site and see the same estimate next to Wikipedia and GDELT.
- Analysis based: stylometry, stock phrases, embeddings vs a pre-2019 human baseline not a verdict on any author as the amount of AI-Generated text increases.

**What you see**

- **How to use** — popup (not `alert()`).
- **Dataset** and **Topic** — pinned left. User can filter based on data source or country or both
- **PDFs or a URL** and **Crawl and analyse** — each add is its own collection. A URL is accepted only if a same-host probe can reach about 500 pages. At most 7 collections addition is allowed, including Wikipedia and GDELT.
- **AI share over time** — yearly estimate; the data is crawled from 2015. Graphs are clustered by data source and country and edges are defined by kNN similarity.
- **Analysis I / II** — live reading lines: AI share, September 2019 era compare, strongest clue.
- **Subject-mix bar chart** — AI generated text present in different phases of data crawled, currently sees: news, schools, places, sports, food, politics, and similar subjects × AI share of that subject.
- **Breakdown by source**.
- **Final analysis** — what likely-AI pages share (embeddings, phrases, words).
- **How we analyze** — methods footer.

Wikipedia and GDELT cannot be deleted. Any other collection can.


## Architecture

One local C++20 process. cpp-httplib serves JSON APIs and static files from `./web` on `0.0.0.0:8080`. Documents live in SQLite (`./data/knowledgeos.db`) with a `dataset_id`. “All sources” and topic filters are query-time views.

```
PDF / URL ingest          Wikipedia seeder          GDELT seeder
        \                       |                        /
         \                      |                       /
                    SQLite  ./data/knowledgeos.db
                                |
         stylometry · detectors · hashed embeddings · calibration
                                |
                    kNN graph (k=8, min cosine 0.32)
                    GraphSAGE on the graph only
                                |
                    query-time: All sources union, country topic
                                |
                         web/index.html
```

**Scoring (per document, then collection)**

- Stylometry (TTR, burstiness, entropy).
- Stock phrases, sentence uniformity, n-gram repetition.
- Embedding anomaly vs the pre-2019 human centroid in the visible set.
- Stylometry deviation from that same baseline.
- Logistic blend + uncertainty interval; three bands: likely AI, likely human, uncertain.
- Pages dated before 2019 are the human baseline. GraphSAGE is **not** a detection signal.

**Collections**

| Collection | Role |
| --- | --- |
| Wikipedia | Seeded, undeletable. Country topics (n ≥ 100). |
| GDELT | Seeded, undeletable. Same country topics. |
| User PDF / URL | New row, never mixed into Wikipedia or GDELT. Deletable. Cap 7 total. |

## Tech stack

| Layer | Current |
| --- | --- |
| Frontend | Single page `web/index.html` (Fraunces + Literata). Copied next to the binary on build. |
| Backend | C++20, CMake 3.20+, cpp-httplib. Binary `build/knowledgeos.exe`. Config in `src_cpp/config.cpp`. |
| Database | SQLite `./data/knowledgeos.db` (`KNOWLEDGEOS_DB`). |
| Embeddings / graph | Hashed embeddings (default dim 128); kNN similarity graph. |
| Deployment | Local process on `0.0.0.0:8080`. Optional: `docker compose up --build` (same port, DB at `./data`). |

There is no streaming product surface (no WebSockets / SSE).

## Run locally

**Prerequisites:** CMake 3.20+, MSVC Build Tools, and an existing `./data/knowledgeos.db` if you want the seeded Wikipedia and GDELT corpora.

**Build** (from the repo root, if `build/knowledgeos.exe` is missing):

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target knowledgeos
```

**Start with crawl off** (use the stored DB; no harvest):

```powershell
$env:CRAWL='false'; $env:KNOWLEDGEOS_WIKIPEDIA_CRAWL='false'; $env:KNOWLEDGEOS_GDELT_CRAWL='false'; .\build\knowledgeos.exe
```

Open [http://localhost:8080](http://localhost:8080).

Omit those flags only if you want a background Wikipedia / GDELT harvest. Defaults in `src_cpp/config.cpp`: crawl **on**, port **8080** (`PORT` / `SERVER_PORT`), DB `./data/knowledgeos.db`.

## Lifecycle of one dataset

1. **Create** — Wikipedia and GDELT are seeded on start. A user specific dataset is created by PDF upload or a public URL (must be open-source public dataset with atleast 1000 pages). Cap 7 collections.
3. **Analyze** — Stylometry, detectors, hashed embeddings, calibration, era scores of all data inclusing the one uploaded by the user (pre- / post–September 2019). Three bands.
4. **Graph** — kNN edges on embedding cosine. GraphSAGE neighborhood embeddings may be stored for the graph
5. **Query-time views** — Dataset = one collection, or **All sources** (union). Topic = country filter on that view.
6. **Dashboard** — Summary readings, time chart, document graph, subject-mix chart, source breakdown, final AI-common analysis.
7. **Delete** — Allowed for any user collection except Wikipedia and GDELT