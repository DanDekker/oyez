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
| `GET /api/v1/ontology` | Topic relationships and ontology calculations |
| `GET /api/v1/topos` | Regional and categorical calculations |
| `GET /api/v1/temporal` | Temporal and topological summaries |
| `GET /api/v1/concepts` | Formal concepts derived from the index |
| `GET /api/v1/pullback`, `/api/v1/pushout` | Relationships between regions |
| `GET /api/v1/fibration` | Category and region relationships |
| `GET /api/v1/operad` | Three-region narrative composition |
| `GET /api/v1/centrality` | Entity and article centrality |
| `GET /api/v1/markets`, `/api/v1/security`, `/api/v1/ai`, `/api/v1/sports`, `/api/v1/weather` | Domain-specific summaries |

## Operations

| Method and path | Description |
| --- | --- |
| `POST /api/v1/catalog/seed` | Add catalog feeds to the index |
| `POST /api/v1/feeds/poll` | Poll feeds and save the snapshot |
| `POST /api/v1/extract` | Extract text and entities from HTML or a URL |
| `POST /api/v1/tokenize` | Encode text with the M1 tokenizer |
| `POST /api/v1/automation/rules` | Create or update a watchpoint |
| `DELETE /api/v1/automation/rules?id=...` | Delete a watchpoint |
| `POST /api/v1/automation/run` | Evaluate watchpoints and optionally poll feeds |

These endpoints have no built-in authentication. Restrict access to trusted clients. For parameter and response details, consult the handlers in [http_server.c](../src/http_server.c).
