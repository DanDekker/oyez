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
  for(size_t pass = 0; pass < 4u; ++pass) {
    for(size_t i = 0; i < count; ++i) {
      buffer_format(&corpus,
                    "%s | %s | %s | %s | %s | %s\n",
                    entries[i].title,
                    entries[i].editorial_focus,
                    entries[i].category,
                    entries[i].country,
                    entries[i].subregion,
                    entries[i].source_tier);
    }
    for(uint32_t n = 0; n < ONTOLOGY_NODE_COUNT; ++n) {
      const OntologyNode *node = ontology_node((OntologyNodeId)n);
      buffer_format(&corpus, "%s : %s\n", node->slug, node->label);
    }
  }
  m1_tokenizer_train(tok, (const uint8_t *)(corpus.data ? corpus.data : ""), corpus.len, 2048u);
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
    aq.query = (i & 1u) ? "semiconductor quantum" : "inflation central bank";
    aq.category = (i & 1u) ? "knowledge-economy" : "public-affairs";
    aq.use_category_morphism = 1;
    aq.limit = 25u;
    aq.offset = i % 8u; /* Vary offset across 16 distinct queries to measure both cold BM25 and hot cache */
    SearchResponse res;
    engine_search(&eng, &aq, &res);
    total_hits += res.total;
    search_response_free(&res);
  }
  uint64_t q1 = monotonic_ns();
  double q_us = ((double)(q1 - q0) / 1e3) / (double)q_iters;
  printf("2. BM25 + Poset Presheaf Fiber + BitSet + Cache Search (%zu queries):\n", q_iters);
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

  uint64_t c0 = monotonic_ns();
  StoryCluster clusters[32];
  size_t cl_count = engine_story_clusters(&eng, 18u, clusters, 32u);
  uint64_t c1 = monotonic_ns();
  printf("4. 4-Band Multi-Probe SimHash64 LSH Story Clustering (%zu articles):\n", N);
  printf("   Total Time:  %.2f ms (clusters formed=%zu)\n",
         (double)(c1 - c0) / 1e6,
         cl_count);

  uint64_t s0 = monotonic_ns();
  double raw_s[ONTOLOGY_NODE_COUNT] = {0};
  double clo_s[ONTOLOGY_NODE_COUNT] = {0};
  double kan_s[ONTOLOGY_NODE_COUNT] = {0};
  double ran_s[ONTOLOGY_NODE_COUNT] = {0};
  double mob_s[ONTOLOGY_NODE_COUNT] = {0};
  double ykan_s[ONTOLOGY_NODE_COUNT] = {0};
  SheafCohomology sheaf;
  double coend_acc = 0.0;
  for(size_t iter = 0; iter < 10000u; ++iter) {
    for(uint32_t k = 0; k < ONTOLOGY_NODE_COUNT; ++k) {
      raw_s[k] = (double)(k + 1u);
      clo_s[k] = (double)((k + 1u) * 3u);
    }
    ontology_left_kan_extend(raw_s, kan_s);
    ontology_right_kan_extend(clo_s, ran_s);
    ontology_mobius_invert(kan_s, mob_s);
    ontology_yoneda_kan_transform(raw_s, ykan_s);
    ontology_sheaf_cohomology(raw_s, clo_s, &sheaf);
    coend_acc += ontology_profunctor_coend(raw_s, clo_s);
  }
  uint64_t s1 = monotonic_ns();
  printf("5. 32-Node Poset Kan + Yoneda + Coend + Helmholtz-Hodge Complex (10,000 runs):\n");
  printf("   Avg Latency: %.3f us/pass (|C|_L=%.2f, grad=%.1f, curl=%.1f, d1*d0=%.2e, coend=%.0f)\n",
         ((double)(s1 - s0) / 1e3) / 10000.0,
         sheaf.leinster_magnitude,
         sheaf.hodge_gradient_norm,
         sheaf.hodge_solenoidal_norm,
         sheaf.cochain_exactness_norm,
         coend_acc / 10000.0);

  uint64_t po0 = monotonic_ns();
  PushoutAmalgam amalgams[32];
  size_t po_cnt = engine_categorical_pushout(&eng, "east-asia", "europe", "topic", amalgams, 32u);
  PresheafMorphism nat_morph;
  engine_natural_transformation(&eng, REGION_EAST_ASIA, REGION_EUROPE, &nat_morph);
  CartesianLift lifts[32];
  size_t fib_cnt = engine_grothendieck_fibration(&eng, "all-news", lifts, 32u);
  TemporalSheafState temp_sheaf;
  engine_temporal_sheaf(&eng, &temp_sheaf);
  PersistenceBarcode barcode;
  ontology_persistent_homology(raw_s, &barcode);
  AdjunctionMonadState monad;
  ontology_adjunction_monad(raw_s, &monad);
  OperadChain chains[16];
  size_t op_cnt = engine_operad_compose(&eng, "east-asia", "north-america", "europe", chains, 16u);
  uint64_t po1 = monotonic_ns();
  printf("6. Pushout, Fibration, Temporal Sheaf, Persistent Homology & Operad:\n");
  printf("   Total Time:  %.3f ms (amalgams=%zu, lifts=%zu, operads=%zu, PH_L1=%.2f, H_kl=%.3f)\n",
         (double)(po1 - po0) / 1e6,
         po_cnt,
         fib_cnt,
         op_cnt,
         barcode.total_persistence_l1,
         monad.kleisli_entropy);

  M1Tokenizer m1;
  uint64_t m0 = monotonic_ns();
  train_default_m1(&m1);
  uint64_t m1_ns = monotonic_ns();
  printf("7. M1 Structured BPE Tokenizer Training + SHA-256 Seal:\n");
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

  if(strcmp(cmd, "tui") == 0 || strcmp(cmd, "top") == 0) {
    double raw_s[ONTOLOGY_NODE_COUNT], clo_s[ONTOLOGY_NODE_COUNT];
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      raw_s[i] = (double)bitset_popcount(&eng.by_direct_ontology[i]);
      clo_s[i] = (double)bitset_popcount(&eng.by_closure_ontology[i]);
    }
    SheafCohomology sheaf;
    ontology_sheaf_cohomology(raw_s, clo_s, &sheaf);
    PersistenceBarcode barcode;
    ontology_persistent_homology(raw_s, &barcode);
    AdjunctionMonadState monad;
    ontology_adjunction_monad(raw_s, &monad);
    TemporalSheafState temp;
    engine_temporal_sheaf(&eng, &temp);

    printf("\033[1;36m========================================================================\033[0m\n");
    printf("\033[1;37m  OYEZ // C23 CATEGORY-THEORETIC & TOPOS INTELLIGENCE TUI DASHBOARD\033[0m\n");
    printf("\033[1;36m========================================================================\033[0m\n");
    printf("  Feeds: \033[1;32m%zu\033[0m | Articles: \033[1;32m%zu\033[0m | Terms: \033[1;32m%zu\033[0m | M1 Vocab: \033[1;33m%u\033[0m\n",
           eng.feed_count, eng.article_count, eng.inv_count, m1_tokenizer_vocab_size(&tok));
    printf("------------------------------------------------------------------------\n");
    printf("  \033[1;35m[Homological & Enriched Category Invariants]\033[0m\n");
    printf("  Leinster Magnitude |C|_L : %8.4f   Euler Char chi(N(C)) : %d\n",
           sheaf.leinster_magnitude, sheaf.euler_characteristic);
    printf("  Fiedler Connectivity l_2 : %8.4f   Cochain ||d1*d0||_2  : %.2e\n",
           sheaf.fiedler_eigenvalue, sheaf.cochain_exactness_norm);
    printf("  Hodge Gradient ||d0*phi||: %8.2f   Hodge Solenoidal Curl: %8.2f\n",
           sheaf.hodge_gradient_norm, sheaf.hodge_solenoidal_norm);
    printf("  Kleisli Monad Entropy    : %8.4f   Zig-Zag Defect       : %.2e\n",
           monad.kleisli_entropy, monad.zig_zag_triangle_defect);
    printf("  Temporal Sheaf Laplacian : %8.4f   Emerging Fiber       : %s\n",
           temp.connection_laplacian_norm, ontology_node(temp.fastest_emerging_node)->slug);
    printf("------------------------------------------------------------------------\n");
    printf("  \033[1;34m[Persistent Homology Filtration Barcode (N_eps(C))]\033[0m\n");
    for(int s = 0; s < PERSISTENCE_FILTRATION_STEPS; ++s) {
      printf("  eps=%.2f | V=%2u E=%3u T=%3u | beta_0=%2u beta_1=%2u | ",
             barcode.steps[s].threshold_eps,
             barcode.steps[s].f_vector[0],
             barcode.steps[s].f_vector[1],
             barcode.steps[s].f_vector[2],
             barcode.steps[s].betti_0,
             barcode.steps[s].betti_1);
      uint32_t bars = barcode.steps[s].betti_0 + barcode.steps[s].betti_1;
      if(bars > 24u) bars = 24u;
      for(uint32_t b = 0; b < bars; ++b) putchar('#');
      putchar('\n');
    }
    printf("\033[1;36m========================================================================\033[0m\n");
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
