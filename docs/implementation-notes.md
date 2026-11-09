# Implementation notes

Oyez combines a production-oriented feed and search pipeline with an experimental mathematical analytics layer, implemented in strict first-party ISO C99 (`-std=c99 -Wall -Wextra -Werror -Wpedantic -O3`) with zero compiler builtins or C11/C23/GNU extensions. This page distinguishes exact finite constructions from numerical summaries so the output can be interpreted correctly.

## System map

| Module | Source | Responsibility |
| --- | --- | --- |
| Umbrella header and static archive | [`oyez.h`](../src/oyez.h), `build/liboyez.a` | Public C99 API and static library target (`make lib`) |
| Core, hashing, and numerical math | [`core.h`](../src/core.h), [`core.c`](../src/core.c) | Buffers, JSON escaping, clocks, hashing, 128-bit limb multiplication, SWAR bit operations, and IEEE-754 binary64 transcendental math (`f64_sqrt`, `f64_ln`, `f64_exp`, `f64_atan`, `f64_cos`) |
| Checksums and wire format | [`checksum.h`](../src/checksum.h), [`checksum.c`](../src/checksum.c), [`wire.h`](../src/wire.h) | CRC-32, SHA-256, and little-endian binary encoding |
| Tokenizers | [`token.h`](../src/token.h), [`token.c`](../src/token.c), [`m1_tokenizer.h`](../src/m1_tokenizer.h), [`m1_tokenizer.c`](../src/m1_tokenizer.c) | Byte-level BPE and the fixed-vocabulary M1 pre-segmenter |
| Parsing and extraction | [`markup.h`](../src/markup.h), [`markup.c`](../src/markup.c) | RSS, Atom, RDF, and JSON Feed parsing; readability scoring; HTML-to-Markdown conversion |
| Ontology and analytics | [`ontology.h`](../src/ontology.h), [`ontology.c`](../src/ontology.c) | Finite-poset operations, ideal algebra, exact sieves, graph and topological summaries, and numerical models |
| Feed catalog and polling | [`catalog.c`](../src/catalog.c), [`feed_poll.c`](../src/feed_poll.c) | Curated source metadata and workers that fetch, parse, and ingest feeds |
| Search engine | [`index_engine.h`](../src/index_engine.h), [`index_engine.c`](../src/index_engine.c) | In-memory BM25+ index, snapshots, filters, similarity search, clustering, ranking, and watchpoints |
| HTTP server and CLI | [`http_server.h`](../src/http_server.h), [`http_server.c`](../src/http_server.c), [`main.c`](../src/main.c) | HTTP/1.1 API, embedded console, and command-line entry points |

## Categorical model

The topic ontology is a finite thin category: its 32 objects form a poset, and there is one morphism `u → v` exactly when topic `u` specializes to topic `v`. The program stores principal upward and downward closures as 32-bit masks and computes the transitive closure during initialization.

The base poset is **not a lattice**. Some object pairs have several minimal common upper bounds, while others have no common lower bound. The exact bound operations therefore return masks containing every minimal upper bound or maximal lower bound. A scalar join or meet exists only when the corresponding mask has one member.

Downward-closed masks form the ideal completion of the topic poset. This is the layer on which the lattice operations are exact:

- join is union of ideals;
- meet is intersection;
- Heyting implication is right adjoint to intersection;
- co-Heyting subtraction is left adjoint to union;
- double negation supplies the implemented modal closure.

For an ideal `A`, the characteristic map is represented exactly at stage `c` by the sieve

```text
χ_A(c) = ↓c ∩ A.
```

Restriction along `d ≤ c` is intersection with `↓d`, so the implementation can test the naturality equation `χ_A(c) ∩ ↓d = χ_A(d)` directly. Any floating-point “truth” value exposed by the analytics layer is a visualization score derived from this sieve, not the subobject-classifier value itself.

The signal-combination routine uses the common-cocone relation `u ≤ c` and `v ≤ c`:

```text
(F ⋆ G)(c) = sup { F(u) + G(v) | u ≤ c and v ≤ c }.
```

This gives a commutative, associative **cocone semipromonoidal convolution**. There is no global unit on unrestricted signal arrays and no representable unit object in the topic poset; on isotone max-plus sections, the constant-zero section is a unit. The routine is not ordinary Day convolution from a monoidal tensor on the topic objects.

## Exact and numerical checks

The test suite exercises finite laws that can be checked directly, including:

- reflexivity, antisymmetry, and transitivity of the topic order;
- minimal-upper-bound and maximal-lower-bound validity;
- closure and adjunction laws in the ideal completion;
- sieve characteristic-map naturality;
- Möbius inversion on the incidence algebra;
- simplicial cochain exactness and selected graph invariants.

Other outputs—transport distances, curvature, persistent summaries, rank-filtered page diagnostics, Isbell-style transforms, and modal factorization diagnostics—are numerical or exploratory models. A zero residual reports that the implemented formula is internally consistent for the supplied data; it is not by itself a proof that Oyez has constructed a full spectral sequence, model category, or Grothendieck topos.

## Storage and retrieval

The index uses a block string arena, open-addressed deduplication, bitset filters, positional BM25+ scoring, a generation-invalidated query cache, and a four-band SimHash index. State is persisted in the `OYEZIX01` binary snapshot; no external database is required.

The feed catalog contains 750 source definitions spanning eleven regions and a broad set of news, research, technology, markets, security, sports, and weather topics. Live `http://` fetching uses a first-party C99 HTTP/1.1 socket client with DNS pinning, chunked/content-length decoding, RFC 3986 redirect resolution, and per-hop SSRF validation (while `https://` TLS transport uses `curl` with identical address, redirect, time, and response-size restrictions); availability still depends on the network and upstream servers.
