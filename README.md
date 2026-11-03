# Oyez

**Oyez! Oyez! Oyez!** — A zero-dependency, first-party news API, RSS/Atom/JSON Feed aggregator, readability extractor, and proprietary search engine built with TypeScript and Bun.

## Features

- **100% First-Party & Zero External Dependencies**: Every parser, tokenizer, indexer, readability scraper, and HTTP router is written from scratch on top of Bun's native runtime.
- **Proprietary Database-Free Index Engine (`OyezIndexEngine`)**:
  - **Okapi BM25 Full-Text Search** with field-weighted postings lists (`title` 3.5x, `categories` 2.5x, `author` 2.0x, `summary` 1.5x, `body` 1.0x), prefix matching, and exact-phrase boosting.
  - **Multi-Dimensional Secondary Indexes** by `feedId`, `category`, `domain`, and binary-searched descending `publishedAt` timeline.
  - **Canonical URL & GUID Deduplication** stripping tracking parameters (`utm_*`, `fbclid`, `gclid`).
  - **Append-Only Write-Ahead Log (`articles.wal.jsonl`) + Compacted Snapshots (`snapshot.oyez.json`)** for instant crash recovery without an external database.
- **Byte-Level BPE & M1 Structured Tokenizer**:
  - Lossless byte-level Byte-Pair Encoding with incremental doubly-linked list + lazy max-heap merge training, rank-order min-heap encoding, reflected CRC-32 (`OYEZBP01`), and SHA-256 artifact sealing (`OYEZMT02`).
- **Unified Feed Parser**:
  - Native support for **RSS 2.0**, **Atom 1.0**, **RSS 1.0 / RDF**, and **JSON Feed 1.1**, including `content:encoded`, `dc:creator`, `media:content`, CDATA sections, and full HTML/XML entity decoding.
- **Readability Full-Text Extractor**:
  - Heuristic DOM container scoring (paragraph density vs. link density), OpenGraph/meta extraction, noise removal, and clean **Markdown** + plain-text generation.
- **Background Poller & Scheduler**:
  - Conditional HTTP fetching (`ETag` / `If-None-Match`, `Last-Modified` / `If-Modified-Since`), bounded concurrency, and exponential backoff on failures.

## Quick Start

```bash
# Start server (default port 3000)
bun run start

# Start and seed with curated default feeds
bun run start -- --seed

# Development mode with hot reload
bun run dev

# Run test suite
bun test
```

## REST API Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/health` | Health status and basic counters |
| `GET` | `/api/v1/stats` | Full index statistics (vocabulary size, postings, categories, domains) |
| `GET` | `/api/v1/feeds` | List all registered feeds and article counts |
| `POST` | `/api/v1/feeds` | Register a new RSS/Atom/JSON feed and poll immediately |
| `POST` | `/api/v1/feeds/seed` | Seed default curated news & technology feeds |
| `POST` | `/api/v1/feeds/refresh` | Poll all enabled feeds on demand |
| `GET` | `/api/v1/feeds/:id` | Get feed metadata and recent articles |
| `PATCH` | `/api/v1/feeds/:id` | Update feed settings (`pollIntervalSeconds`, `scrapeFullContent`, `enabled`) |
| `DELETE` | `/api/v1/feeds/:id` | Delete a feed and purge its indexed articles |
| `POST` | `/api/v1/feeds/:id/refresh` | Poll a single feed immediately |
| `GET` | `/api/v1/articles` | Search and filter articles (`q`, `feedId`, `category`, `domain`, `from`, `to`, `sort=relevance\|date`, `limit`, `offset`) |
| `GET` | `/api/v1/articles/:id` | Retrieve full article details and extracted Markdown |
| `DELETE` | `/api/v1/articles/:id` | Remove an article from the index |
| `POST` | `/api/v1/articles/:id/extract` | Fetch and extract full readability content for an indexed article |
| `POST` | `/api/v1/extract` | Standalone readability extraction for any `{ "url": "..." }` or `{ "html": "..." }` |
| `POST` | `/api/v1/tokenize` | Encode text with the M1/BPE tokenizer and inspect lexical stems |
| `POST` | `/api/v1/snapshot` | Trigger an immediate compacted disk snapshot |

## License

MIT
