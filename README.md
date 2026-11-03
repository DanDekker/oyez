# Oyez

**Oyez! Oyez! Oyez!** — A zero-dependency, first-party global news API, RSS/Atom/JSON Feed aggregator, readability extractor, and category-theoretic deep search engine built with TypeScript and Bun.

## Architecture & Highlights

- **100% First-Party & Zero External Dependencies**: Every parser, tokenizer, indexer, readability scraper, and HTTP router is written from scratch on top of Bun's native runtime.
- **Global News Catalog (`WORLD_FEED_CATALOG`)**:
  - 100+ curated feeds across all **11 World Regions** (`north-america`, `latin-america`, `europe`, `middle-east`, `africa`, `south-asia`, `east-asia`, `southeast-asia`, `central-asia`, `oceania`, `global`) and **45+ countries**.
- **Epistemic News Types & Geopolitical Gazetteer**:
  - Classifies every article into one of 10 journalistic genres (`breaking-dispatch`, `investigative-report`, `policy-and-legislative`, `scientific-research`, `market-and-economic`, `opinion-and-editorial`, `explainer-and-analysis`, `press-and-official`, `cultural-and-feature`, `general-report`).
  - Built-in geopolitical gazetteer extracts mentioned ISO-2 countries, macro topics, international organizations, and proper noun entities.
- **Category Theory & Algebraic Information Retrieval**:
  - **Ontology Poset Category $\mathcal{C}$ & Contravariant Presheaf Indexing ($F: \mathcal{C}^{\text{op}} \to \mathbf{FinSet}$)**: Categories form a directed acyclic meet/join poset with diamond inheritance (`semiconductors-and-hardware` $\preceq$ `trade-and-supply-chain` $\preceq$ `economy-and-markets` and `technology-and-computing`). Articles are automatically indexed across their upward principal filter $\uparrow c$, guaranteeing functorial fiber inclusion $F(u) \subseteq F(v)$ whenever $u \preceq v$.
  - **Left Kan Extension ($\text{Lan}_\iota S$)**: Propagates relevance and frequency valuations upward along ontology morphisms under both the **Tropical Max-Plus Semiring** and **Archimedean Weighted-Sum Semiring**.
  - **Antitone Galois Connection & Formal Concept Analysis (FCA)**: Extracts closed **Formal Concepts** $(\text{Extent}, \text{Intent})$ where $A'' = A$ and $B = A'$ along with Hasse subconcept/superconcept lattice edges.
  - **Categorical Pullbacks ($X \times_Z Y$)**: Constructs the universal fibered product of articles between any two regions, countries, or news types over a shared pivot dimension (`topic`, `mentionedCountry`, `entity`, `ontologyCategory`).
- **Advanced Retrieval & Graph Algorithms**:
  - **Okapi BM25 Full-Text Search** with field-weighted postings (`title` 3.5x, `entities/topics` 3.0x, `categories` 2.5x, `author` 2.0x, `summary` 1.5x, `body` 1.0x), prefix expansion, and phrase boosting.
  - **64-bit Charikar SimHash + $O(1)$ Bitwise Hamming LSH**: Near-duplicate detection and locality-sensitive similarity scoring.
  - **Submodular Maximum Marginal Relevance (MMR)**: Greedy submodular diversification (`?diversify=true&mmrLambda=0.7`) balancing query relevance against source, country, and SimHash redundancy.
  - **Bipartite Entity-Article PageRank**: Power-iteration eigenvector centrality over the global entity-topic-article graph.
- **Byte-Level BPE & M1 Structured Tokenizer**:
  - Lossless byte-level Byte-Pair Encoding with incremental doubly-linked list + lazy max-heap merge training, rank-order min-heap encoding, reflected CRC-32 (`OYEZBP01`), and SHA-256 artifact sealing (`OYEZMT02`).

## Quick Start

```bash
# Start server (default port 3000)
bun run start

# Start and seed with the global feed catalog
bun run start -- --seed

# Poll all global feeds and build the deep index in ./data
bun run index:world

# Run test suite
bun test
```

## REST API Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/health` | Health status and index counters |
| `GET` | `/api/v1/stats` | Full index statistics, vocabulary size, postings, and facet distributions |
| `GET` | `/api/v1/facets` | Live facet counts (`newsTypes`, `regions`, `countries`, `mentionedCountries`, `ontologyCategories`, `topics`, `topEntities`) |
| `GET` | `/api/v1/trending` | Multi-source global story clusters and top trending entities/topics |
| `GET` | `/api/v1/ontology` | Ontology Poset Hasse diagram, Presheaf fiber counts, Left Kan Extensions, and Meet/Join (`?u=...&v=...`) |
| `GET` | `/api/v1/concepts` | Formal Concept Lattice via Galois Connection closure (`?minSupport=2&limit=20`) |
| `GET` | `/api/v1/pullback` | Categorical Pullback $X \times_Z Y$ across fibers (`?leftRegion=europe&rightRegion=east-asia&pivot=topic`) |
| `GET` | `/api/v1/centrality` | Bipartite Entity-Article PageRank eigenvector centrality |
| `GET` | `/api/v1/catalog` | Browse the built-in 100+ global feed catalog (`?region=...&country=...&category=...`) |
| `GET` | `/api/v1/feeds` | List registered feeds and article counts |
| `POST` | `/api/v1/feeds` | Register a new RSS/Atom/JSON feed |
| `POST` | `/api/v1/feeds/seed` | Seed feeds from the global catalog (filterable by `region`, `country`, `category`) |
| `POST` | `/api/v1/feeds/refresh` | Poll all enabled feeds on demand |
| `GET` | `/api/v1/articles` | Search & filter articles (`q`, `newsType`, `category`, `region`, `country`, `mentionedCountry`, `topic`, `entity`, `diversify=true`, `mmrLambda=0.7`) |
| `GET` | `/api/v1/articles/:id` | Retrieve full article details, entities, SimHash, and Markdown |
| `GET` | `/api/v1/articles/:id/related` | Cross-country related articles covering the same story |
| `GET` | `/api/v1/articles/:id/duplicates` | Near-duplicate lookup via 64-bit SimHash Hamming distance (`?maxDistance=14`) |
| `POST` | `/api/v1/articles/:id/extract` | Fetch and extract full readability content for an indexed article |
| `POST` | `/api/v1/extract` | Standalone readability extraction for any `{ "url": "..." }` or `{ "html": "..." }` |
| `POST` | `/api/v1/tokenize` | Encode text with the M1/BPE tokenizer and inspect lexical stems |
| `POST` | `/api/v1/snapshot` | Trigger an immediate compacted disk snapshot |

## License

MIT
