#ifndef OYEZ_H
#define OYEZ_H

/*
 * Oyez — Unified Public C99 Developer Header
 *
 * Single-include umbrella header exposing the complete Oyez surface:
 *   - Core arena/buffer/time/string primitives and wire framing (core.h, wire.h, checksum.h)
 *   - Reversible byte-level and M1 32K BPE/WordPiece/Unigram tokenizers (token.h, m1_tokenizer.h)
 *   - RSS/Atom/JSON Feed parser and HTML readability/Markdown extractor (markup.h)
 *   - 32-node finite poset ontology, sheaf/topos/spectral/transport invariants (ontology.h)
 *   - Curated 321-feed global intelligence catalog (catalog.h)
 *   - In-memory roaring bitset & BM25F/LSH/PageRank/HITS/Sinkhorn/Bandit engine (index_engine.h)
 *   - Concurrent HTTP/1.1 client, feed poller, and JSON/UI server (http_server.h)
 */

#define OYEZ_VERSION_MAJOR 2
#define OYEZ_VERSION_MINOR 4
#define OYEZ_VERSION_PATCH 0
#define OYEZ_VERSION_STRING "2.4.0"

#include "core.h"
#include "wire.h"
#include "checksum.h"
#include "token.h"
#include "m1_tokenizer.h"
#include "markup.h"
#include "ontology.h"
#include "catalog.h"
#include "index_engine.h"
#include "http_server.h"

#endif
