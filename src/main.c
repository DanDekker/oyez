#include "catalog.h"
#include "core.h"
#include "http_server.h"
#include "index_engine.h"
#include "m1_tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void ensure_data_dir(void) {
  mkdir("data", 0755);
}

static void train_default_m1(M1Tokenizer *tok) {
  m1_tokenizer_init(tok);
  size_t count = 0;
  const CatalogEntry *entries = catalog_entries(&count);
  Buffer corpus;
  buffer_init(&corpus);
  for(size_t i = 0; i < count; ++i) {
    buffer_format(&corpus, "%s | %s | %s\n", entries[i].title, entries[i].editorial_focus, entries[i].category);
  }
  m1_tokenizer_train(tok, (const uint8_t *)(corpus.data ? corpus.data : ""), corpus.len, 512u);
  buffer_free(&corpus);
}

static int run_bench(void) {
  printf("================================================================\n");
  printf("  Oyez Pure C23 Proprietary Engine Benchmark Suite\n");
  printf("================================================================\n");

  OyezEngine eng;
  engine_init(&eng);
  size_t seeded = engine_seed_catalog(&eng, NULL, NULL);

  const char *topics[] = {
      "Semiconductor TSMC foundry wafer lithography and quantum computing",
      "Central bank inflation interest rate monetary policy bond yields",
      "Supreme Court constitutional ruling antitrust legislation parliament",
      "Climate change carbon emissions renewable solar grid battery storage",
      "Investigative whistleblower audit offshore financial secrecy",
      "Naval defense maritime security ceasefire diplomatic summit",
      "Artificial intelligence neural network open source kernel linux",
      "Epidemiology clinical trial vaccine public health hospital"};

  size_t N = 2000u;
  uint64_t t0 = monotonic_ns();
  for(size_t i = 0; i < N; ++i) {
    uint32_t fid = eng.feeds[i % eng.feed_count].id;
    char guid[64], title[256], url[256], body[512];
    snprintf(guid, sizeof(guid), "bench-guid-%zu", i);
    snprintf(title, sizeof(title), "Dispatch #%zu: %s", i, topics[i % 8u]);
    snprintf(url, sizeof(url), "https://example.org/news/%zu", i);
    snprintf(body,
             sizeof(body),
             "<article><p>In-depth dispatch from Tokyo, Brussels, and Washington covering %s. "
             "United Nations and European Union officials reviewed the policy framework.</p></article>",
             topics[(i + 3u) % 8u]);
    engine_ingest_article(&eng,
                          fid,
                          guid,
                          title,
                          url,
                          "Oyez Bureau",
                          title,
                          body,
                          "2026-11-03T00:00:00Z",
                          1762128000000ULL + i,
                          "",
                          NULL,
                          0,
                          NULL,
                          NULL);
  }
  uint64_t t1 = monotonic_ns();
  double ingest_ms = (double)(t1 - t0) / 1e6;
  printf("1. Ingest + Entity/Poset/SimHash64 Index (%zu articles across %zu feeds):\n", N, seeded);
  printf("   Total Time:  %.2f ms (%.0f articles/sec)\n", ingest_ms, ((double)N * 1000.0) / ingest_ms);

  uint64_t q0 = monotonic_ns();
  size_t q_iters = 1000u;
  size_t total_hits = 0;
  for(size_t i = 0; i < q_iters; ++i) {
    ArticleQuery aq;
    memset(&aq, 0, sizeof(aq));
    aq.query = "semiconductor quantum";
    aq.category = "knowledge-economy";
    aq.use_category_morphism = 1;
    aq.limit = 25u;
    SearchResponse res;
    engine_search(&eng, &aq, &res);
    total_hits += res.total;
    search_response_free(&res);
  }
  uint64_t q1 = monotonic_ns();
  double q_us = ((double)(q1 - q0) / 1e3) / (double)q_iters;
  printf("2. BM25 + Poset Presheaf Fiber + BitSet Search (%zu queries):\n", q_iters);
  printf("   Avg Latency: %.2f us/query (%.0f queries/sec, hits=%zu)\n",
         q_us,
         1e6 / q_us,
         total_hits / q_iters);

  uint64_t p0 = monotonic_ns();
  RankedEntity ents[24];
  ScoredArticle arts[16];
  size_t ecount = engine_bipartite_pagerank(&eng, 15u, 0.85, ents, 24u, arts, 16u);
  uint64_t p1 = monotonic_ns();
  printf("3. Bipartite Entity-Article PageRank (15 iterations over %zu articles):\n", N);
  printf("   Total Time:  %.2f ms (top entity: %s score=%.4f)\n",
         (double)(p1 - p0) / 1e6,
         ecount > 0 ? ents[0].id : "none",
         ecount > 0 ? ents[0].score : 0.0);

  M1Tokenizer m1;
  uint64_t m0 = monotonic_ns();
  train_default_m1(&m1);
  uint64_t m1_ns = monotonic_ns();
  printf("4. M1 Structured BPE Tokenizer Training + SHA-256 Seal:\n");
  printf("   Vocab Size:  %u (trained in %.2f ms)\n",
         m1_tokenizer_vocab_size(&m1),
         (double)(m1_ns - m0) / 1e6);

  m1_tokenizer_free(&m1);
  engine_free(&eng);
  return 0;
}

int main(int argc, char **argv) {
  const char *cmd = argc > 1 ? argv[1] : "serve";
  if(strcmp(cmd, "bench") == 0) {
    return run_bench();
  }

  ensure_data_dir();
  const char *snap_path = "data/oyez.ix";
  const char *tok_path = "data/oyez.m1";

  OyezEngine eng;
  engine_init(&eng);
  if(engine_load_snapshot(&eng, snap_path) != 0) {
    engine_seed_catalog(&eng, NULL, NULL);
    engine_save_snapshot(&eng, snap_path);
  }

  M1Tokenizer tok;
  m1_tokenizer_init(&tok);
  if(m1_tokenizer_load(&tok, tok_path) != 0) {
    train_default_m1(&tok);
    m1_tokenizer_save(&tok, tok_path);
  }

  if(strcmp(cmd, "seed") == 0) {
    size_t added = engine_seed_catalog(&eng, NULL, NULL);
    engine_save_snapshot(&eng, snap_path);
    printf("seeded %zu new feeds (total=%zu)\n", added, eng.feed_count);
    m1_tokenizer_free(&tok);
    engine_free(&eng);
    return 0;
  }

  if(strcmp(cmd, "poll") == 0) {
    size_t limit = argc > 2 ? (size_t)strtoul(argv[2], NULL, 10) : 12u;
    size_t ins = engine_poll_all(&eng, limit);
    engine_save_snapshot(&eng, snap_path);
    printf("polled %zu feeds -> ingested %zu articles (total=%zu)\n", limit, ins, eng.article_count);
    m1_tokenizer_free(&tok);
    engine_free(&eng);
    return 0;
  }

  if(strcmp(cmd, "stats") == 0) {
    printf("feeds: %zu\narticles: %zu\ninverted_terms: %zu\nm1_vocab: %u\nm1_sha256: %s\n",
           eng.feed_count,
           eng.article_count,
           eng.inv_count,
           m1_tokenizer_vocab_size(&tok),
           tok.sha256);
    m1_tokenizer_free(&tok);
    engine_free(&eng);
    return 0;
  }

  uint16_t port = 8787u;
  const char *env_port = getenv("PORT");
  if(env_port && env_port[0]) port = (uint16_t)strtoul(env_port, NULL, 10);
  if(argc > 2) port = (uint16_t)strtoul(argv[2], NULL, 10);

  ServerContext ctx = {&eng, &tok, snap_path, port};
  int rc = http_server_listen(&ctx);
  m1_tokenizer_free(&tok);
  engine_free(&eng);
  return rc;
}
