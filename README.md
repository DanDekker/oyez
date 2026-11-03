# Oyez

**Oyez** is a zero-dependency, first-party **C23** global news aggregator API, RSS/Atom/JSON Feed parser, readability content extractor, category-theoretic ontology engine, and in-memory BM25 + 64-bit BitSet search engine.

Compiled strictly with `-std=c23 -Wall -Wextra -Werror -Wpedantic -O3` and only `-lm -lpthread`.

---

## Architecture & Proprietary C23 Stack

| Module | File | Description |
| :--- | :--- | :--- |
| **Core & Hashing** | [`src/core.h`](src/core.h), [`src/core.c`](src/core.c) | Dynamic/fixed `Buffer`, JSON string escaping, monotonic nanosecond clocks, `fnv1a64`, `mix64`, hardware `popcnt64` & `ctz64`. |
| **Checksums & Wire** | [`src/checksum.h`](src/checksum.h), [`src/checksum.c`](src/checksum.c), [`src/wire.h`](src/wire.h) | Little-endian binary wire encoding, reflected nibble CRC-32, and incremental SHA-256 (`Sha256Digest`). |
| **Byte-Level BPE Tokenizer** | [`src/token.h`](src/token.h), [`src/token.c`](src/token.c) | Doubly-linked symbol list + open-addressed `PairTable` + `MaxHeap`/`MinHeap` BPE trainer/encoder with `OYEZBP01` CRC-32 binary checkpoints. |
| **M1 Structured Tokenizer** | [`src/m1_tokenizer.h`](src/m1_tokenizer.h), [`src/m1_tokenizer.c`](src/m1_tokenizer.c) | Fixed-vocabulary pre-segmenter (256 raw bytes + 26 indent tokens + 200 syntax/crypto/scientific/AI-kernel primitives + 120 news/arXiv/cybersec/macro/neocloud/inference landmarks + 16 control tokens = `618` base IDs, `2,048` default trained vocabulary) with `OYEZMT02` SHA-256 + CRC-32 sealed serialization. |
| **Markup & Readability** | [`src/markup.h`](src/markup.h), [`src/markup.c`](src/markup.c) | Unified RSS 2.0, Atom 1.0, RSS 1.0/RDF, and JSON Feed 1.1 parser + Readability DOM block scorer and HTML-to-Markdown converter. |
| **Category Theory & Gazetteer** | [`src/ontology.h`](src/ontology.h), [`src/ontology.c`](src/ontology.c) | **32-node Bitmask Poset Category $\mathcal{C}$** (`upward_mask` $\uparrow c$, `downward_mask` $\downarrow c$), Lattice Join $\vee$ & Meet $\wedge$, Heyting Algebra Implication $(A \Rightarrow B)$ & Intuitionistic Negation $\neg A$, Left & Right Kan Extensions ($\text{Lan}_\iota \dashv \iota^* \dashv \text{Ran}_\iota$), **Rota's Incidence Algebra Möbius Inversion** $\mu(u, v)$, **Simplicial Nerve $N(\mathcal{C})$ $f$-vector & Euler Characteristic $\chi(N(\mathcal{C})) = 1$**, **Combinatorial Hodge Laplacian $L_0$ Spectrum & Dirichlet Energy**, **Poset Wasserstein / Radon Distance**, Grothendieck Sheaf Cohomology $H^1$ defect, 103-entry domain acronym/synonym expander, **138-country ISO-2 gazetteer**, **26 Frontier & Open-Weights AI Model families**, **36 Neocloud / Inference / Hugging Face / AI Silicon families**, **31 Hugging Face Hub `<org>/<model>` repository namespace extractors**, **5-tier Open-Source (OSI) vs Open-Weights vs Closed-Source Frontier API license spectrum (`AiWeightLicense`)**, and 64-bit Charikar SimHash. |
| **Global Feed Catalog** | [`src/catalog.h`](src/catalog.h), [`src/catalog.c`](src/catalog.c) | **588 curated news, frontier science, AI labs, Hugging Face Hub, open-weights/local inference, GPU neoclouds, inference engines, 0-day cybersec, browser/anti-bot standards, central banks, equities, FX & fiscal event feeds** across all 11 world regions and 100+ countries/territories. |
| **BM25 & BitSet Index Engine** | [`src/index_engine.h`](src/index_engine.h), [`src/index_engine.c`](src/index_engine.c) | Database-free in-memory + `OYEZIX01` binary snapshot engine with 64-bit word `BitSet` presheaf fibers (including `by_ai_tier`, `by_ai_license`, `by_ai_model`, `by_ai_infra`, `by_hf_repo`, `by_cve`, `by_org`, `by_asset`, `by_fiscal_event`, `by_clearance`), **4-Band Multi-Probe SimHash64 LSH** index, **64-entry Generation-Invalidated Query Cache**, Okapi BM25 ranking, Submodular MMR diversification, Galois Formal Concept Analysis, Categorical Pullbacks, and Bipartite Entity-Article PageRank. |
| **POSIX HTTP Server & CLI** | [`src/http_server.h`](src/http_server.h), [`src/http_server.c`](src/http_server.c), [`src/main.c`](src/main.c) | Zero-dependency POSIX socket HTTP/1.1 JSON REST server, **multi-threaded `pthread` concurrent feed poller**, and CLI (`serve`, `seed`, `poll`, `bench`, `stats`). |

---

## Performance Benchmarks (Apple Silicon M-Series, `-O3`)

```text
================================================================
  Oyez Pure C23 Proprietary Engine Benchmark Suite
================================================================
1. Ingest + Entity/Poset/SimHash64 Index (2000 articles across 524 feeds):
   Total Time:  550.20 ms (3635 articles/sec)
2. BM25 + Poset Presheaf Fiber + BitSet + Cache Search (1000 queries):
   Avg Latency: 0.43 us/query (2298851 queries/sec, hits=546)
3. Bipartite Entity-Article PageRank (15 iterations over 2000 articles):
   Total Time:  1.11 ms (top entity: geo:US score=0.0661)
4. 4-Band Multi-Probe SimHash64 LSH Story Clustering (2000 articles):
   Total Time:  0.02 ms (clusters formed=32)
5. 32-Node Poset Left/Right Kan Extensions & Sheaf Cohomology (10,000 runs):
   Avg Latency: 0.381 us/pass (L2 cohomology norm=757777.00)
6. M1 Structured BPE Tokenizer Training + SHA-256 Seal:
   Vocab Size:  2048 (trained in 215.05 ms)
```

---

## Quick Start

```bash
# Build binary and run test suite
make test

# Run the high-performance benchmark suite
make bench

# Seed the 524 global news, scientific, cybersec & macro/market feeds into data/oyez.ix
./build/oyez seed

# Poll live RSS/Atom feeds
./build/oyez poll 15

# Start the HTTP/1.1 REST API server on port 8787
./build/oyez serve 8787
```

---

## REST API Endpoints

- `GET /health` — Engine health, feed/article counts, and inverted index vocabulary size
- `GET /api/v1/stats` — Region, epistemic `NewsType`, and Poset presheaf fiber counts
- `GET /api/v1/ontology?sub=ai&sup=knowledge-economy&a=cyber&b=ai` — Poset Category $\mathcal{C}$ morphisms, Lattice Join $\vee$ / Meet $\wedge$, and Left Kan Extension $\text{Lan}_\iota$ scores
- `GET /api/v1/concepts?min_support=2` — Formal Concept Analysis (Galois connection $\mathcal{P}(\mathcal{G}) \leftrightarrows \mathcal{P}(\mathcal{M})^{\text{op}}$)
- `GET /api/v1/pullback?left_region=east-asia&right_region=europe&over=mentioned_country` — Categorical fibered product $X \times_Z Y$ across regional bureaus
- `GET /api/v1/centrality` — Bipartite Entity-Article PageRank centrality
- `GET /api/v1/markets` — Macro & fiscal stance fiber counts, extracted FX/equity/commodity asset counts, and fiscal event counts
- `GET /api/v1/security` — Clearance level fiber counts, active `CVE-YYYY-NNNN` vulnerability fiber counts, and global organization fiber counts
- `GET /api/v1/ai` — AI Compute Tier fiber counts (`frontier-model-and-rl`, `neocloud-and-cluster`, `inference-and-serving`, `applied-and-rag`), Open-Source vs Closed-Source `ai_license_spectrum` (`open-source-osi-full`, `open-weights-permissive`, `open-weights-custom-license`, `closed-frontier-api`), `open_vs_closed_summary`, extracted frontier/open-weights AI model fibers, neocloud/inference/GPU infrastructure fibers, Hugging Face `<org>/<model>` repository fibers, and top AI compute articles
- `GET /api/v1/catalog?region=east-asia` — Browse the 588-feed global news catalog
- `POST /api/v1/catalog/seed` — Seed feeds from the global catalog
- `GET /api/v1/feeds` — List indexed feeds
- `POST /api/v1/feeds/poll` — Poll live feeds and persist `OYEZIX01` snapshot
- `GET /api/v1/articles?q=semiconductor&category=knowledge-economy&category_morphism=1&diversify=1` — Okapi BM25 + Poset Presheaf restriction + Submodular MMR search (supports `?ai_model=`, `?ai_infra=`, `?ai_tier=`, `?ai_license=`, `?hf_repo=`, `?cve=`, `?org=`, `?asset=`, `?fiscal_event=`)
- `GET /api/v1/articles/:id` — Retrieve full article record
- `GET /api/v1/articles/:id/similar` — 64-bit Charikar SimHash LSH (`popcnt64`) near-duplicate search
- `POST /api/v1/extract` — Extract clean text, Markdown, and entities from raw HTML or URL
- `POST /api/v1/tokenize` — Encode text with the M1 structured BPE tokenizer
