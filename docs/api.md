# API reference

The server defaults to port `8787`. Examples assume it is running on the same machine. Data returned by search and analytics depends on the feeds and articles in the current snapshot.

```sh
curl http://localhost:8787/health
curl 'http://localhost:8787/api/v1/catalog?region=east-asia'
curl 'http://localhost:8787/api/v1/articles?q=semiconductor&category=knowledge-economy&limit=5'
curl 'http://localhost:8787/api/v1/feed.xml?category=ai'
```

## Reading data

| Method and path | Description |
| --- | --- |
| `GET /`, `/health` | Health and index counts |
| `GET /console` | Embedded browser console |
| `GET /api/v1/catalog` | Feed catalog, with optional `region` |
| `GET /api/v1/feeds` | Indexed feeds |
| `GET /api/v1/articles` | Article search and filters; supports `q`, `category`, `exclude_category`, `limit`, `offset`, `category_morphism`, and `diversify` |
| `GET /api/v1/articles/:id` | Article details |
| `GET /api/v1/articles/:id/similar` | Similar articles |
| `GET /api/v1/feed.xml` | Atom feed, with optional `category` |
| `GET /api/v1/stream` | Server-sent events |
| `GET /api/v1/stats` | Index and category statistics |
| `GET /api/v1/appliance`, `/api/v1/machine` | Engine state and self-checks |
| `GET /api/v1/facets`, `/api/v1/trending` | Category facets |
| `GET /api/v1/automation` | Watchpoints, alerts, and feed priorities |

## Analysis

| Method and path | Description |
| --- | --- |
| `GET /api/v1/ontology` | Finite-poset relationships, bound candidates, and ideal operations |
| `GET /api/v1/topos` | Ideal algebra, cocone convolution, transport, and sieve-derived categorical diagnostics |
| `GET /api/v1/temporal` | Temporal, filtration, and topological summaries |
| `GET /api/v1/concepts` | Formal concepts derived from the index |
| `GET /api/v1/pullback`, `/api/v1/pushout` | Relationships between regions |
| `GET /api/v1/fibration` | Category and region relationships |
| `GET /api/v1/operad` | Three-region narrative composition |
| `GET /api/v1/centrality` | Entity and article centrality |
| `GET /api/v1/markets`, `/api/v1/security`, `/api/v1/ai`, `/api/v1/sports`, `/api/v1/weather` | Domain-specific summaries |

## Operations

| Method and path | Description |
| --- | --- |
| `POST /api/v1/catalog/seed` | Add catalog feeds to the index; accepts optional `region` and `country` (query string or JSON body) |
| `POST /api/v1/feeds/poll` | Poll feeds and save the snapshot; accepts optional `limit` (query string or JSON body, default `16`) |
| `POST /api/v1/articles/:id/enrich` | Enrich an indexed article in place from `{"html":"..."}`, `{"url":"..."}`, or its stored source URL |
| `POST /api/v1/extract` | Extract text, markdown, and entities from HTML or a URL; accepts optional `article_id` for in-place enrichment |
| `POST /api/v1/tokenize` | Encode text with the M1 tokenizer |
| `POST /api/v1/automation/rules` | Create or update a watchpoint |
| `DELETE /api/v1/automation/rules` | Delete a watchpoint by `id` (query string or JSON body) |
| `POST /api/v1/automation/run` | Evaluate watchpoints and optionally poll feeds via `poll_batch` and `auto_poll_interval_sec` (query string or JSON body) |

Use `oyez api` to call GET and POST routes in-process without starting the TCP server. A path alone sends GET; supplying a JSON body sends POST. To send DELETE, use an HTTP client against the running server.

```sh
./build/oyez api '/api/v1/ontology?sub=ai&sup=knowledge-economy&a=ai&b=space'
./build/oyez api /api/v1/catalog/seed '{"region":"east-asia","country":"TW"}'
```

These endpoints have no built-in authentication. Restrict access to trusted clients. For parameter and response details, consult the handlers in [http_server.c](../src/http_server.c).

The topic ontology is a finite poset, not an object-level lattice. When a pair has several minimal upper bounds or maximal lower bounds, the ontology response reports every candidate and leaves the unique join or meet as `null`. Lattice operations are exact on downward-closed ideals. The implementation represents exact characteristic values as sieve masks; the API's scalar truth scores, rank-page fields, and factorization fields are derived diagnostics.

### Category API compatibility

The accurate v1 names are `order`, `subobject_classifier`, `cocone_convolution`, `modal_closure_diagnostic`, `rank_page_diagnostic`, and the appliance `consistency_status`/`checks_*` fields. Deprecated `lattice`, `subobject_classifier_omega`, `day_convolution`, `quillen_model_factorization`, `spectral_sequence`, generic appliance `status`, and theorem-named appliance fields remain as response aliases for existing clients. Deprecated objects carry `deprecated: true` where their shape permits it; `status_deprecated` marks the scalar status alias.

The CLI likewise prints `consistency_checks` and `max_numerical_residual` first, followed by the deprecated `theorems_verified` and `max_numerical_defect` aliases for scripts written against earlier output.

One intentional correctness break remains: an ambiguous legacy `lattice.join` is now `null` instead of an arbitrary topic. C callers should use `ontology_unique_join`; the deprecated scalar `ontology_join` returns `ONTO_NONE` when no unique join exists. The deprecated `ontology_day_*` entry points now delegate to the cocone operation, so callers that relied on the former arbitrary-join numerical behavior must migrate explicitly.
