# Oyez

Oyez! Oyez! Oyez! A news API and RSS aggregator for the people.

**Oyez** is a zero-dependency, first-party **C23** global news aggregator API, RSS/Atom/JSON Feed parser, readability content extractor, category-theoretic ontology engine, and in-memory BM25 + 64-bit BitSet search engine.

Compiled strictly with `-std=c23 -Wall -Wextra -Werror -Wpedantic -O3` and only `-lm -lpthread`.

---

## Architecture & Proprietary C23 Stack

| Module | File | Description |
| :--- | :--- | :--- |
| **Core & Hashing** | [`src/core.h`](src/core.h), [`src/core.c`](src/core.c) | Dynamic/fixed `Buffer`, JSON string escaping, monotonic nanosecond clocks, `fnv1a64`, `mix64`, 128-bit multiplicative `wyhash64`, hardware `popcnt64`, `ctz64` & `clz64`. |
| **Checksums & Wire** | [`src/checksum.h`](src/checksum.h), [`src/checksum.c`](src/checksum.c), [`src/wire.h`](src/wire.h) | Little-endian binary wire encoding, reflected nibble CRC-32, and incremental SHA-256 (`Sha256Digest`). |
| **Byte-Level BPE Tokenizer** | [`src/token.h`](src/token.h), [`src/token.c`](src/token.c) | Doubly-linked symbol list + open-addressed `PairTable` + `MaxHeap`/`MinHeap` BPE trainer/encoder with `OYEZBP01` CRC-32 binary checkpoints. |
| **M1 Structured Tokenizer** | [`src/m1_tokenizer.h`](src/m1_tokenizer.h), [`src/m1_tokenizer.c`](src/m1_tokenizer.c) | Fixed-vocabulary pre-segmenter (256 raw bytes + 26 indent tokens + 200 syntax/crypto/scientific/AI-kernel primitives + 144 news/arXiv/DeepMind/cybersec/macro/politics/sports landmarks + 16 control tokens = `642` base IDs, `2,048` default trained vocabulary) with **O(1) First-Byte Length-Sorted Dispatch Table (`g_m1_dispatch[256]`)**, **64-bit M1 Subword Structural SimHash (`m1_structural_sketch64`)**, and `OYEZMT02` SHA-256 + CRC-32 sealed serialization. |
| **Markup & Readability** | [`src/markup.h`](src/markup.h), [`src/markup.c`](src/markup.c) | Unified RSS 2.0, Atom 1.0, RSS 1.0/RDF, and JSON Feed 1.1 parser + Readability DOM block scorer and HTML-to-Markdown converter. |
| **Category Theory & Gazetteer** | [`src/ontology.h`](src/ontology.h), [`src/ontology.c`](src/ontology.c) | **32-node Bitmask Poset Category $\mathcal{C}$** (`upward_mask` $\uparrow c$, `downward_mask` $\downarrow c$), Lattice Join $\vee$ & Meet $\wedge$, Heyting Algebra Implication $(A \Rightarrow B)$ & Intuitionistic Negation $\neg A$, **Lawvere-Tierney Double-Negation Topology $j(A) = \neg\neg A$ & Grothendieck Dense Sieves**, **Lawvere Enriched Quasimetric Space $d_{\mathcal{L}}(u, v)$ via Tropical Min-Plus Semiring Kleene Star**, **Leinster Magnitude $|\mathcal{C}|_{\mathcal{L}} = \sum_u w(u)$ & Maximum-Entropy Weighting**, **Yoneda Embedding $\mathcal{Y}: \mathcal{C} \hookrightarrow \mathbf{Set}^{\mathcal{C}^{\text{op}}}$, Constructive Yoneda Lemma Isomorphism $\text{Nat}(\mathcal{Y}(c), F) \cong F(c)$ & Enriched Lawvere-Yoneda Kan Transform**, **Monad & Comonad Algebras ($T = \iota^* \text{Lan}_\iota$, $G = \iota^* \text{Ran}_\iota$) + Zig-Zag Triangle Identity Verification & Kleisli Entropy**, **Persistent Homology Filtration Barcodes ($\beta_0(\epsilon_k), \beta_1(\epsilon_k)$ across 8 Lawvere/Density Scales)**, Left & Right Kan Extensions ($\text{Lan}_\iota \dashv \iota^* \dashv \text{Ran}_\iota$), **Rota's Incidence Algebra Möbius Inversion** $\mu(u, v)$, **Profunctor Coend $\int^{c \in \mathcal{C}} F(c) G(c)$**, **Simplicial Nerve $N(\mathcal{C})$ $f$-vector, Betti Numbers $(\beta_0, \beta_1) = (1, 0)$ & Euler Characteristic $\chi(N(\mathcal{C})) = 1$**, **Simplicial Cochain Complex $\delta_1 \circ \delta_0 = 0$, Discrete Helmholtz-Hodge Decomposition ($L_0 \phi = \delta_0^\top F$ into Exact Gradient Flow $\delta_0 \phi$ & Divergence-Free Solenoidal Curl $F - \delta_0 \phi$), Combinatorial Hodge Laplacian $L_0$ Spectrum, Fiedler Algebraic Connectivity $\lambda_2(L_0)$ & Dirichlet Energy**, **Poset Wasserstein / Radon Distance**, **Word-Start Bucket Index (`WordBloomIndex`) with 512-bit Cache-Line Bloom Filter & 32-Bucket Offset Chains**, 103-entry domain acronym/synonym expander, **138-country ISO-2 gazetteer**, **31 Frontier, Google DeepMind Research & Open-Weights AI Model families**, **37 Neocloud / Inference / JAX / Hugging Face / AI Silicon families**, **34 Hugging Face Hub `<org>/<model>` repository namespace extractors**, **5-tier Open-Source (OSI) vs Open-Weights vs Closed-Source Frontier API license spectrum (`AiWeightLicense`)**, **25 Global Sports Leagues & Tournaments**, and 64-bit Charikar SimHash. |
| **Global Feed Catalog** | [`src/catalog.h`](src/catalog.h), [`src/catalog.c`](src/catalog.c) | **648 curated news, frontier science, Google DeepMind research, AI labs, Hugging Face Hub, open-weights/local inference, GPU neoclouds, inference engines, 0-day cybersec, browser/anti-bot standards, central banks, equities, FX, fiscal/political events & global sports feeds** across all 11 world regions and 100+ countries/territories. |
| **BM25+ & BitSet Index Engine** | [`src/index_engine.h`](src/index_engine.h), [`src/index_engine.c`](src/index_engine.c) | Thread-safe (`pthread_rwlock_t`) database-free in-memory + `OYEZIX01` binary snapshot engine with **Contiguous 64 KB Block String Arena (`StringArenaBlock`)**, O(1) `wyhash64` open-addressed `dedup_table`, 64-bit word `BitSet` presheaf fibers (`bitset_andnot_inplace`, `bitset_intersect_count`, `bitset_jaccard`), **4-Band Multi-Probe SimHash64 LSH** index, **64-entry Generation-Invalidated Query Cache**, **BM25+ ($\delta = 1.0$) with 32-Bit Positional Proximity Phrase Boosting** and categorical specificity boost, Submodular MMR diversification, **Galois Formal Concept Lattice Hasse Diagrams**, **BitSet-accelerated Categorical Pullbacks ($X \times_Z Y$) & Pushout Colimits ($X \sqcup_Z Y$)**, **Presheaf Natural Transformations ($\eta: F \Rightarrow G$)**, **Grothendieck Construction / Category of Elements $\int_{\mathcal{C}} F$ & Cartesian Lifting (`CartesianLift`)**, **Temporal Cellular Sheaves over Time Intervals (`TemporalSheafState`)**, **Symmetric Monoidal Operad 3-Stage Narrative Composition (`OperadChain`)**, and Bipartite Entity-Article PageRank. |
| **POSIX HTTP Server & CLI** | [`src/http_server.h`](src/http_server.h), [`src/http_server.c`](src/http_server.c), [`src/main.c`](src/main.c) | Zero-dependency multi-threaded (`pthread`) HTTP/1.1 Keep-Alive server with **Embedded HTML5 Topos Intelligence Console (`GET /console`)**, **Atom 1.0 XML Syndication (`GET /api/v1/feed.xml`)**, **Server-Sent Events (`GET /api/v1/stream`)**, concurrent `pthread` feed poller, and CLI (`serve`, `seed`, `poll`, `bench`, `stats`, `tui`). |

---

## Performance Benchmarks (Apple Silicon M-Series, `-O3`)

```text
================================================================
  Oyez Pure C23 Proprietary Engine Benchmark Suite
================================================================
1. Ingest + Entity/Poset/SimHash64 Index (2000 articles across 647 feeds):
   Total Time:  50.52 ms (39585 articles/sec)
2. BM25 + Poset Presheaf Fiber + BitSet + Cache Search (1000 queries):
   Avg Latency: 0.66 us/query (1524390 queries/sec, hits=548)
3. Bipartite Entity-Article PageRank (15 iterations over 2000 articles):
   Total Time:  1.42 ms (top entity: geo:US score=0.0562)
4. 4-Band Multi-Probe SimHash64 LSH Story Clustering (2000 articles):
   Total Time:  0.02 ms (clusters formed=32)
5. 32-Node Poset Kan + Yoneda + Coend + Helmholtz-Hodge Complex (10,000 runs):
   Avg Latency: 11.228 us/pass (|C|_L=15.67, grad=57120.6, curl=2169.4, d1*d0=0.00e+00, coend=40239)
6. Pushout, Fibration, Temporal Sheaf, Persistent Homology & Operad:
   Total Time:  0.166 ms (amalgams=5, lifts=32, operads=16, PH_L1=1.25, H_kl=2.917)
7. M1 Structured BPE Tokenizer Training + SHA-256 Seal:
   Vocab Size:  2048 (trained in 99.97 ms)
```

---

## Quick Start

```bash
# Build binary and run test suite
make test

# Run the high-performance benchmark suite
make bench

# Launch the ANSI Category-Theoretic & Topos TUI Dashboard
./build/oyez tui

# Seed the 648 global news, scientific, cybersec, AI & sports feeds into data/oyez.ix
./build/oyez seed

# Poll live RSS/Atom feeds
./build/oyez poll 15

# Start the HTTP/1.1 REST API & Web Console server on port 8787
./build/oyez serve 8787
```

---

## REST API & Web Endpoints

- `GET /console` — Zero-dependency embedded HTML5/JS Category-Theoretic & Topos Intelligence Console
- `GET /health` — Engine health, feed/article counts, and inverted index vocabulary size
- `GET /api/v1/stats` — Region, epistemic `NewsType`, and Poset presheaf fiber counts
- `GET /api/v1/ontology?sub=ai&sup=knowledge-economy&a=cyber&b=ai` — Poset Category $\mathcal{C}$ morphisms, Möbius $\mu(u,v)$, Lattice Join $\vee$ / Meet $\wedge$, Heyting Implication / Negation, Lawvere-Tierney Sheafification $j(A) = \neg\neg A$, Lawvere Quasimetric $d_{\mathcal{L}}(A, B)$, Leinster Magnitude $|\mathcal{C}|_{\mathcal{L}}$, Enriched Lawvere-Yoneda Kan Transform, Simplicial Nerve $f$-vector, Betti numbers, Fiedler eigenvalue $\lambda_2(L_0)$, Discrete Helmholtz-Hodge Decomposition (`hodge_gradient_norm`, `hodge_solenoidal_norm`), Cochain Exactness $\|\delta_1\delta_0 f\|_2^2$, Profunctor Coend, and Left/Right Kan Extensions
- `GET /api/v1/temporal` — Persistent Homology Filtration Barcode ($\beta_0(\epsilon_k), \beta_1(\epsilon_k)$), Adjunction Monad/Comonad State ($\eta, \varepsilon$, Zig-Zag Triangle Identity defect, Kleisli entropy), and Temporal Cellular Sheaf velocity/acceleration over time stalks
- `GET /api/v1/operad?r1=east-asia&r2=north-america&r3=europe` — Symmetric Monoidal Operad 3-stage narrative composition ($A \xrightarrow{w_{12}} B \xrightarrow{w_{23}} C$)
- `GET /api/v1/concepts?min_support=2` — Formal Concept Analysis (Galois connection $\mathcal{P}(\mathcal{G}) \leftrightarrows \mathcal{P}(\mathcal{M})^{\text{op}}$, Lawvere-Tierney closure, intensional entropy, and Concept Lattice Hasse Diagram covering morphisms)
- `GET /api/v1/pullback?left_region=east-asia&right_region=europe&over=mentioned_country` — Categorical fibered product $X \times_Z Y$ across regional bureaus
- `GET /api/v1/pushout?left_region=east-asia&right_region=europe&over=mentioned_country` — Categorical pushout colimit $X \sqcup_Z Y$ amalgamated equivalence classes and contravariant Presheaf Natural Transformation $\eta: F_{\text{left}} \Rightarrow F_{\text{right}}$
- `GET /api/v1/fibration?category=all-news` — Grothendieck Construction / Category of Elements $\int_{\mathcal{C}} F$ Cartesian lifts $(u, a_u) \to (v, a_v)$ over covering morphisms $u \to v$ in $\mathcal{C}$
- `GET /api/v1/feed.xml?category=ai` — Live Atom 1.0 XML syndication stream for downstream feed readers
- `GET /api/v1/stream` — Server-Sent Events (`text/event-stream`) topological & article telemetry stream
- `GET /api/v1/centrality` — Bipartite Entity-Article PageRank centrality
- `GET /api/v1/markets` — Macro & fiscal stance fiber counts, extracted FX/equity/commodity asset counts, and fiscal event counts
- `GET /api/v1/security` — Clearance level fiber counts, active `CVE-YYYY-NNNN` vulnerability fiber counts, and global organization fiber counts
- `GET /api/v1/ai` — AI Compute Tier fiber counts (`frontier-model-and-rl`, `neocloud-and-cluster`, `inference-and-serving`, `applied-and-rag`), Open-Source vs Closed-Source `ai_license_spectrum` (`open-source-osi-full`, `open-weights-permissive`, `open-weights-custom-license`, `closed-frontier-api`), `open_vs_closed_summary`, extracted frontier/open-weights AI model fibers, neocloud/inference/GPU infrastructure fibers, Hugging Face `<org>/<model>` repository fibers, and top AI compute articles
- `GET /api/v1/sports` — Global sports articles count, 25 extracted league/tournament fiber counts (`UEFA-Champions-League`, `Premier-League`, `Formula-1`, `NBA`, `NFL`, `FIFA-World-Cup`, `ATP/WTA-Grand-Slam`, `Olympic-Games`, etc.), and top sports articles
- `GET /api/v1/catalog?region=east-asia` — Browse the 648-feed global news catalog
- `POST /api/v1/catalog/seed` — Seed feeds from the global catalog
- `GET /api/v1/feeds` — List indexed feeds
- `POST /api/v1/feeds/poll` — Poll live feeds and persist `OYEZIX01` snapshot
- `GET /api/v1/articles?q=semiconductor&category=knowledge-economy&exclude_category=politics&category_morphism=1&diversify=1` — BM25+ + Poset Presheaf restriction + Heyting/BitSet difference exclusion + Submodular MMR search (supports `?ai_model=`, `?ai_infra=`, `?ai_tier=`, `?ai_license=`, `?hf_repo=`, `?sport=`, `?cve=`, `?organization=`, `?asset=`, `?fiscal_event=`)
- `GET /api/v1/articles/:id` — Retrieve full article record
- `GET /api/v1/articles/:id/similar` — 64-bit Charikar SimHash LSH (`popcnt64`) near-duplicate search
- `POST /api/v1/extract` — Extract clean text, Markdown, and entities from raw HTML or URL
- `POST /api/v1/tokenize` — Encode text with the M1 structured BPE tokenizer
