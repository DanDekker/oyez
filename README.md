<p align="center"><img src="assets/oyez-banner.svg" alt="Oyez — a news engine in C. Collect, index, explore." width="100%"></p>

# Oyez

**A self-contained news aggregation and search engine written in pure first-party C99.**

Oyez collects RSS, Atom, and JSON feeds, extracts article text, and indexes it for search. It combines a curated feed catalog, an in-memory search engine, an HTTP API, and a browser console in one program. Its experimental analytics explore relationships between news topics, regions, and entities.

[Quick start](#quick-start) · [API](#api) · [Architecture](#architecture) · [Implementation notes](docs/implementation-notes.md)

## What it does

- **Collect:** seed a catalog of 750 feeds and poll sources for articles.
- **Search:** query a BM25+ index, filter by topic and region, and find similar articles using SimHash.
- **Explore:** inspect topic, geographic, entity, and temporal summaries through the API or embedded console.
- **Experiment:** use watchpoint rules, feed scheduling, tokenization, finite-poset and ideal-algebra constructions, and numerical models.
- **Run locally:** keep the index in memory and save it as a binary snapshot; no database is required.

## Quick start

You need a POSIX environment, `make`, and a compiler that accepts `-std=c99`. The program links only the system pthread library (`-lpthread`), implementing its own IEEE-754 floating-point, transcendental, hashing, and 128-bit arithmetic routines in pure C99. Plaintext `http://` feeds are fetched directly by the first-party C99 HTTP/1.1 socket client; `https://` feed fetching uses `curl` on `PATH` and network access.

```sh
git clone https://github.com/DanDekker/oyez.git
cd oyez
make                 # builds build/liboyez.a, build/oyez, and build/test_oyez
make test            # runs the test suite
./build/oyez serve 8787
```

Open [localhost:8787/console](http://localhost:8787/console) for the browser console, or query the API from another terminal:

```sh
curl http://localhost:8787/health
curl 'http://localhost:8787/api/v1/articles?q=semiconductor&limit=5'
```

On first launch, Oyez initializes the feed catalog and tokenizer under `data/` in the current working directory. A new index may have no articles until feeds are polled. To poll a batch before starting the server, run `./build/oyez poll 15`; this contacts external feed providers. Feed availability and results depend on the network and upstream sources.

> **Network access:** The server binds to all network interfaces and provides state-changing endpoints without built-in authentication. Use it only on a trusted network or restrict access at the host/network boundary; do not expose it directly to the public internet.

## API

| Endpoint | Purpose |
| --- | --- |
| `GET /health` | Health and feed, article, and index counts |
| `GET /api/v1/catalog` | Browse the feed catalog; accepts `region` |
| `GET /api/v1/feeds` | List feeds in the index |
| `GET /api/v1/articles` | Search and filter indexed articles |
| `GET /api/v1/articles/:id` | Retrieve an article |
| `GET /api/v1/articles/:id/similar` | Find similar articles |
| `POST /api/v1/articles/:id/enrich` | Enrich an article in place from HTML or its source URL |
| `GET /api/v1/feed.xml` | Export an Atom feed |
| `GET /api/v1/stats` | Inspect index and category statistics |
| `POST /api/v1/extract` | Extract content from HTML or a URL |
| `POST /api/v1/tokenize` | Tokenize text with the M1 tokenizer |
| `GET /api/v1/automation` | Inspect watchpoint rules and alerts |

The API also exposes ontology, temporal, geographic, and domain-specific analytics, along with endpoints for polling feeds and managing watchpoints. See the [API reference](docs/api.md) for the endpoint list and examples.

## Command line

```sh
./build/oyez seed [region] [country]      # add catalog feeds to the snapshot
./build/oyez poll [limit]                 # poll up to limit feeds (default: 12)
./build/oyez search <query> [category]    # search indexed articles via BM25+ & presheaf fibers
./build/oyez extract <html_or_url>        # extract readable text, markdown, and entities
./build/oyez tokenize <text>              # encode text with the M1 BPE tokenizer
./build/oyez api <path> [json_body]       # run a GET or POST API route in-process
./build/oyez stats                        # show index and engine statistics
./build/oyez tui                          # print the terminal dashboard
./build/oyez automate [batch]             # evaluate watchpoints and optionally poll feeds
make bench                                # run the synthetic benchmark suite
```

The benchmark uses generated articles and repeated searches, including cached queries; it is useful for comparing changes on the same machine, not as a measure of real-world throughput.

## Embedding as a C99 library

Build the static library (`build/liboyez.a`) with `make lib` and include the single umbrella header [`src/oyez.h`](src/oyez.h). Save this example as `example.c`:

```c
#include "oyez.h"

#include <stdio.h>

int main(void) {
    OyezEngine eng;
    engine_init(&eng);
    engine_seed_catalog(&eng, "east-asia", "TW");

    ArticleQuery q;
    article_query_init(&q);
    q.query = "semiconductor";
    q.category = "knowledge-economy";

    SearchResponse res;
    int rc = engine_search(&eng, &q, &res);
    if (rc == 0) {
        printf("%zu matches\n", res.total);
    }
    search_response_free(&res);
    engine_free(&eng);
    return rc == 0 ? 0 : 1;
}
```

From the repository root, compile and run it:

```sh
cc -std=c99 -Isrc example.c build/liboyez.a -pthread -o example
./example
```

The example seeds feed definitions only, so it prints `0 matches` until articles are ingested or loaded from a populated snapshot.

## Architecture

```text
 RSS / Atom / JSON feeds                 HTTP API + browser console
            │                                      ▲
            ▼                                      │
   fetch → parse → extract → index → search / analytics
                                │
                         binary snapshot
```

| Component | Source |
| --- | --- |
| Public C99 header and static archive | [oyez.h](src/oyez.h), `build/liboyez.a` |
| Feed catalog, fetching, and polling | [catalog.c](src/catalog.c), [url_fetch.c](src/url_fetch.c), [feed_poll.c](src/feed_poll.c) |
| Feed parsing and text extraction | [markup.c](src/markup.c) |
| In-memory index, ranking, snapshots, and automation | [index_engine.c](src/index_engine.c) |
| Topic poset, ideal completion, exact sieves, and numerical models | [ontology.c](src/ontology.c) |
| Byte-pair and M1 tokenizers | [token.c](src/token.c), [m1_tokenizer.c](src/m1_tokenizer.c) |
| HTTP server, embedded console, and CLI | [http_server.c](src/http_server.c), [main.c](src/main.c) |
| Tests | [test_oyez.c](tests/test_oyez.c) |

The [implementation notes](docs/implementation-notes.md) distinguish exact finite constructions from exploratory numerical diagnostics and describe the underlying data structures.
