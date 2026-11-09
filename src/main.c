#include "oyez.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void print_usage(FILE *out) {
  fputs("Usage: oyez [command] [arguments]\n"
        "\n"
        "  serve [port]               Start the HTTP server (default: 8787)\n"
        "  seed [region] [country]    Add catalog feeds to the snapshot\n"
        "  poll [limit]               Poll feeds (default: 12)\n"
        "  search <query> [category]  Search indexed articles via BM25+ & presheaf fibers\n"
        "  extract <html_or_url>      Extract readable content and domain entities\n"
        "  tokenize <text>            Encode text with the M1 BPE tokenizer\n"
        "  api <path> [json_body]     Execute any HTTP API endpoint in-process\n"
        "  automate [batch]           Evaluate watchpoints and optionally poll feeds\n"
        "  weather                    Show weather diagnostics\n"
        "  stats                      Show engine statistics\n"
        "  tui                        Show the terminal dashboard\n"
        "  bench                      Run the synthetic benchmarks\n"
        "  version                    Show version information\n"
        "  help                       Show this help\n",
        out);
}

static int parse_port(const char *value, uint16_t *port) {
  unsigned parsed = 0;
  if(!value[0]) return 0;
  for(const char *p = value; *p; ++p) {
    if(*p < '0' || *p > '9') return 0;
    unsigned digit = (unsigned)(*p - '0');
    if(parsed > (65535u - digit) / 10u) return 0;
    parsed = parsed * 10u + digit;
  }
  if(parsed == 0u) return 0;
  *port = (uint16_t)parsed;
  return 1;
}

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
  printf("  Oyez Pure C99 Proprietary Engine Benchmark Suite\n");
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
      "NOAA NHC Hurricane Category 4 Tornado Warning Solar Flare X-Class USGS M7.2"};

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
  ToposState topos;
  engine_topos_state(&eng, REGION_EAST_ASIA, REGION_EUROPE, ONTO_AI, ONTO_TECHNOLOGY, &topos);
  uint64_t po1 = monotonic_ns();
  printf("6. Finite Poset, Ideal Algebra, Cocone Convolution, Transport & Rank Diagnostics:\n");
  printf("   Total Time:  %.3f ms (amalgams=%zu, lifts=%zu, operads=%zu, W_eps=%.3f, S_eps=%.3f, d_FR=%.3f, cocone_resid=%.1e)\n",
         (double)(po1 - po0) / 1e6,
         po_cnt,
         fib_cnt,
         op_cnt,
         topos.sinkhorn_ot.entropic_wasserstein,
         topos.sinkhorn_ot.sinkhorn_divergence,
         topos.sinkhorn_ot.fisher_rao_geodesic,
         topos.cocone_adjunction_defect);

  uint64_t wx0 = monotonic_ns();
  WeatherPhysicsState phys;
  ontology_atmospheric_dynamics(28.5, 74.0, 16.0, 36.0, 4.5, &phys);
  BanditFeedPriority prio[16];
  size_t bandit_cnt = engine_bandit_select_feeds(&eng, prio, 16u);
  uint64_t wx1 = monotonic_ns();
  printf("7. Lorenz-63 Chaos RK4 + Benettin QR Lyapunov & UCB1 Bandit Automation:\n");
  printf("   Total Time:  %.3f ms (lambda_1=%.4f, D_KY=%.4f, T_w=%.2fC, K_s=%.2f, alerts=%zu, bandit_k=%zu)\n",
         (double)(wx1 - wx0) / 1e6,
         phys.lyapunov_max,
         phys.kaplan_yorke_dim,
         phys.wet_bulb_celsius,
         phys.rossby_stationary_k,
         eng.automation_alert_count,
         bandit_cnt);

  M1Tokenizer m1;
  uint64_t m0 = monotonic_ns();
  train_default_m1(&m1);
  uint64_t m1_ns = monotonic_ns();
  printf("8. M1 Structured BPE Tokenizer Training + SHA-256 Seal:\n");
  printf("   Vocab Size:  %u (trained in %.2f ms)\n",
         m1_tokenizer_vocab_size(&m1),
         (double)(m1_ns - m0) / 1e6);

  uint64_t ap0 = monotonic_ns();
  EngineApplianceState app;
  engine_appliance_state(&eng, &app);
  uint64_t ap1 = monotonic_ns();
  printf("9. Consolidated One-Machine Appliance Consistency & Memory Audit:\n");
  printf("   Total Time:  %.3f ms (checks=%u/%u, resident=%.2f MB, arena=%.1f KB, max_residual=%.2e)\n",
         (double)(ap1 - ap0) / 1e6,
         app.checks_passed,
         app.checks_total,
         (double)app.total_resident_bytes / (1024.0 * 1024.0),
         (double)app.arena_bytes_used / 1024.0,
         app.max_numerical_residual);

  m1_tokenizer_free(&m1);
  engine_free(&eng);
  return 0;
}

typedef struct {
  OyezEngine eng;
  M1Tokenizer tok;
  const char *snap_path;
  const char *tok_path;
} CliRuntime;

static void cli_runtime_init(CliRuntime *rt) {
  ensure_data_dir();
  rt->snap_path = "data/oyez.ix";
  rt->tok_path = "data/oyez.m1";

  engine_init(&rt->eng);
  if(engine_load_snapshot(&rt->eng, rt->snap_path) != 0) {
    engine_seed_catalog(&rt->eng, NULL, NULL);
    engine_save_snapshot(&rt->eng, rt->snap_path);
  }

  m1_tokenizer_init(&rt->tok);
  if(m1_tokenizer_load(&rt->tok, rt->tok_path) != 0) {
    train_default_m1(&rt->tok);
    m1_tokenizer_save(&rt->tok, rt->tok_path);
  }
}

static void cli_runtime_free(CliRuntime *rt) {
  m1_tokenizer_free(&rt->tok);
  engine_free(&rt->eng);
}

static int run_serve(int argc, char **argv) {
  uint16_t port = 8787u;
  const char *port_arg = argc > 2 ? argv[2] : getenv("PORT");
  if(port_arg && (argc > 2 || port_arg[0]) && !parse_port(port_arg, &port)) {
    fprintf(stderr, "Invalid port: %s (expected 1-65535)\n", port_arg);
    return 2;
  }
  CliRuntime rt;
  cli_runtime_init(&rt);
  ServerContext ctx = {&rt.eng, &rt.tok, rt.snap_path, port};
  int rc = http_server_listen(&ctx);
  cli_runtime_free(&rt);
  return rc;
}

static int cmd_seed(CliRuntime *rt, int argc, char **argv) {
  const char *region = (argc > 2 && argv[2][0]) ? argv[2] : NULL;
  const char *country = (argc > 3 && argv[3][0]) ? argv[3] : NULL;
  size_t added = engine_seed_catalog(&rt->eng, region, country);
  engine_save_snapshot(&rt->eng, rt->snap_path);
  printf("seeded %zu new feeds (total=%zu)\n", added, rt->eng.feed_count);
  return 0;
}

static int cmd_poll(CliRuntime *rt, int argc, char **argv) {
  size_t limit = argc > 2 ? (size_t)strtoul(argv[2], NULL, 10) : 12u;
  size_t ins = engine_poll_all(&rt->eng, limit);
  engine_save_snapshot(&rt->eng, rt->snap_path);
  printf("polled %zu feeds -> ingested %zu articles (total=%zu)\n",
         limit, ins, rt->eng.article_count);
  return 0;
}

static int cmd_search(CliRuntime *rt, int argc, char **argv) {
  ArticleQuery aq;
  article_query_init(&aq);
  aq.query = (argc > 2 && argv[2][0]) ? argv[2] : NULL;
  aq.category = (argc > 3 && argv[3][0]) ? argv[3] : NULL;
  aq.limit = 15u;

  SearchResponse res;
  engine_search(&rt->eng, &aq, &res);
  printf("total: %zu | returned: %zu | query_time_ms: %.4f\n",
         res.total, res.count, res.query_time_ms);
  for(size_t i = 0; i < res.count; ++i) {
    const ArticleRecord *a = res.hits[i].article;
    printf("  [%u] (score=%.3f, %s/%s) %s\n      %s\n",
           a->id,
           res.hits[i].score,
           a->feed_country[0] ? a->feed_country : "GL",
           news_type_slug(a->news_type),
           a->title ? a->title : "",
           a->url ? a->url : "");
  }
  search_response_free(&res);
  return 0;
}

static int cmd_extract(CliRuntime *rt, int argc, char **argv) {
  if(argc <= 2 || !argv[2][0]) {
    fprintf(stderr, "Usage: oyez extract <html_or_url>\n");
    return 2;
  }
  const char *arg = argv[2];
  Buffer body_buf;
  buffer_init(&body_buf);
  if(strncmp(arg, "http://", 7u) == 0 || strncmp(arg, "https://", 8u) == 0) {
    buffer_text(&body_buf, "{\"url\":");
    json_cstr(&body_buf, arg);
    buffer_text(&body_buf, "}");
  } else {
    buffer_text(&body_buf, "{\"html\":");
    json_cstr(&body_buf, arg);
    buffer_text(&body_buf, "}");
  }
  ServerContext ctx = {&rt->eng, &rt->tok, rt->snap_path, 0};
  int status = 200;
  Buffer out;
  http_handle_request(&ctx, "POST", "/api/v1/extract",
                      body_buf.data ? body_buf.data : "{}", body_buf.len,
                      &status, &out);
  printf("%s\n", out.data ? out.data : "{}");
  buffer_free(&out);
  buffer_free(&body_buf);
  return status >= 400 ? 1 : 0;
}

static int cmd_tokenize(CliRuntime *rt, int argc, char **argv) {
  if(argc <= 2) {
    fprintf(stderr, "Usage: oyez tokenize <text>\n");
    return 2;
  }
  const char *text = argv[2];
  uint32_t *tokens = NULL;
  size_t count = 0;
  m1_tokenizer_encode(&rt->tok, (const uint8_t *)text, strlen(text), &tokens, &count);
  printf("{\"vocab_size\":%u,\"sha256\":\"%s\",\"token_count\":%zu,\"tokens\":[",
         m1_tokenizer_vocab_size(&rt->tok), rt->tok.sha256, count);
  for(size_t i = 0; i < count; ++i) {
    if(i > 0) putchar(',');
    printf("%u", tokens[i]);
  }
  printf("]}\n");
  free(tokens);
  return 0;
}

static int cmd_api(CliRuntime *rt, int argc, char **argv) {
  if(argc <= 2 || !argv[2][0]) {
    fprintf(stderr, "Usage: oyez api <path> [json_body]\n");
    return 2;
  }
  const char *target = argv[2];
  const char *body = argc > 3 ? argv[3] : "";
  const char *method = argc > 3 ? "POST" : "GET";
  ServerContext ctx = {&rt->eng, &rt->tok, rt->snap_path, 0};
  int status = 200;
  Buffer out;
  http_handle_request(&ctx, method, target, body, strlen(body), &status, &out);
  printf("%s\n", out.data ? out.data : "");
  buffer_free(&out);
  return status >= 400 ? 1 : 0;
}

static int cmd_automate(CliRuntime *rt, int argc, char **argv) {
  size_t batch = argc > 2 ? (size_t)strtoul(argv[2], NULL, 10) : 0u;
  BanditFeedPriority prio[8];
  size_t sel = engine_bandit_select_feeds(&rt->eng, prio, 8u);
  printf("UCB1 Bandit Top-%zu Priority Feeds:\n", sel);
  for(size_t i = 0; i < sel; ++i) {
    printf("  [%zu] feed_id=%u ucb1=%.3f yield=%.2f cat=%s (%s)\n",
           i + 1u, prio[i].feed_id, prio[i].ucb1_score, prio[i].empirical_yield,
           prio[i].category, prio[i].title);
  }
  size_t ins = 0;
  if(batch > 0u) {
    ins = engine_poll_bandit_auto(&rt->eng, batch, 4u);
  } else {
    engine_evaluate_all_automation_rules(&rt->eng);
    rt->eng.auto_cycles_completed++;
  }
  engine_save_snapshot(&rt->eng, rt->snap_path);
  printf("Automation cycle #%llu complete: newly_ingested=%zu active_rules=%zu total_alerts=%zu\n",
         (unsigned long long)rt->eng.auto_cycles_completed, ins,
         rt->eng.automation_rule_count, rt->eng.automation_alert_count);
  size_t show = rt->eng.automation_alert_count < 5u ? rt->eng.automation_alert_count : 5u;
  for(size_t k = 0; k < show; ++k) {
    size_t idx = (rt->eng.automation_alert_head + MAX_AUTOMATION_ALERTS - 1u - k) % MAX_AUTOMATION_ALERTS;
    const AutomationAlert *al = &rt->eng.automation_alerts[idx];
    printf("  ALERT #%llu [rule=%s score=%.2f]: %s (%s)\n",
           (unsigned long long)al->alert_id, al->rule_name, al->trigger_score,
           al->article_title, al->weather_severity_slug);
  }
  return 0;
}

static int cmd_weather(CliRuntime *rt, int argc, char **argv) {
  (void)argc; (void)argv;
  WeatherPhysicsState phys;
  ontology_atmospheric_dynamics(26.0, 70.0, 18.0, 38.0, 3.5, &phys);
  printf("Weather/Climate Fiber Articles: %u | Lorenz-63 Attractor: (%.3f, %.3f, %.3f)\n",
         bitset_popcount(&rt->eng.by_closure_ontology[ONTO_CLIMATE]),
         phys.lorenz_state[0], phys.lorenz_state[1], phys.lorenz_state[2]);
  printf("Max Lyapunov lambda_1: %.4f | Kaplan-Yorke Dim D_KY: %.4f | Horizon: %.3f\n",
         phys.lyapunov_max, phys.kaplan_yorke_dim, phys.predictability_horizon);
  printf("Saturation Vapor e_s: %.2f hPa | Wet-Bulb T_w: %.2f C | Rossby K_s: %.2f (blocking=%s)\n",
         phys.saturation_vapor_hpa, phys.wet_bulb_celsius, phys.rossby_stationary_k,
         phys.omega_blocking_regime ? "YES" : "NO");
  return 0;
}

static int cmd_stats(CliRuntime *rt, int argc, char **argv) {
  (void)argc; (void)argv;
  EngineApplianceState app;
  engine_appliance_state(&rt->eng, &app);
  printf("feeds: %zu\narticles: %zu\ninverted_terms: %zu\nm1_vocab: %u\nm1_sha256: %s\n"
         "consistency_checks: %u/%u (%s)\nmax_numerical_residual: %.3e\n"
         "theorems_verified: %u/%u (%s)\nmax_numerical_defect: %.3e\n"
         "resident_memory_bytes: %zu (%.2f MB)\narena_used_bytes: %zu / %zu (%zu blocks)\n"
         "inverted_index_bytes: %zu\nbitset_fiber_bytes: %zu\nlsh_and_dedup_bytes: %zu\n"
         "automation_rules: %zu\nautomation_alerts: %zu\n",
         rt->eng.feed_count,
         rt->eng.article_count,
         rt->eng.inv_count,
         m1_tokenizer_vocab_size(&rt->tok),
         rt->tok.sha256,
         app.checks_passed,
         app.checks_total,
         app.all_checks_passed ? "PASS" : "DEGRADED",
         app.max_numerical_residual,
         app.checks_passed,
         app.checks_total,
         app.all_checks_passed ? "PASS" : "DEGRADED",
         app.max_numerical_residual,
         app.total_resident_bytes,
         (double)app.total_resident_bytes / (1024.0 * 1024.0),
         app.arena_bytes_used,
         app.arena_bytes_capacity,
         app.arena_block_count,
         app.inverted_index_bytes,
         app.bitset_fiber_bytes,
         app.lsh_and_dedup_bytes,
         app.active_watchpoint_rules,
         app.total_watchpoint_alerts);
  return 0;
}

static int cmd_tui(CliRuntime *rt, int argc, char **argv) {
  (void)argc; (void)argv;
  EngineApplianceState app;
  engine_appliance_state(&rt->eng, &app);
  double raw_s[ONTOLOGY_NODE_COUNT], clo_s[ONTOLOGY_NODE_COUNT];
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    raw_s[i] = (double)bitset_popcount(&rt->eng.by_direct_ontology[i]);
    clo_s[i] = (double)bitset_popcount(&rt->eng.by_closure_ontology[i]);
  }
  SheafCohomology sheaf;
  ontology_sheaf_cohomology(raw_s, clo_s, &sheaf);
  PersistenceBarcode barcode;
  ontology_persistent_homology(raw_s, &barcode);
  AdjunctionMonadState monad;
  ontology_adjunction_monad(raw_s, &monad);
  TemporalSheafState temp;
  engine_temporal_sheaf(&rt->eng, &temp);
  ToposState topos;
  engine_topos_state(&rt->eng, REGION_EAST_ASIA, REGION_EUROPE, ONTO_AI, ONTO_TECHNOLOGY, &topos);
  WeatherPhysicsState wx_phys;
  ontology_atmospheric_dynamics(26.0, 70.0, 18.0, 38.0, 3.5, &wx_phys);

  printf("\033[1;36m========================================================================\033[0m\n");
  printf("\033[1;37m  OYEZ // C99 ONE-MACHINE AUTONOMOUS INTELLIGENCE APPLIANCE DASHBOARD\033[0m\n");
  printf("\033[1;36m========================================================================\033[0m\n");
  printf("  Feeds: \033[1;32m%zu\033[0m | Articles: \033[1;32m%zu\033[0m | Terms: \033[1;32m%zu\033[0m | M1 Vocab: \033[1;33m%u\033[0m\n",
         rt->eng.feed_count, rt->eng.article_count, rt->eng.inv_count, m1_tokenizer_vocab_size(&rt->tok));
  printf("  Resident RAM: \033[1;32m%.2f MB\033[0m (Arena: %.1f/%.1f KB | InvIdx: %.1f KB | Fibers: %.1f KB)\n",
         (double)app.total_resident_bytes / (1024.0 * 1024.0),
         (double)app.arena_bytes_used / 1024.0,
         (double)app.arena_bytes_capacity / 1024.0,
         (double)app.inverted_index_bytes / 1024.0,
         (double)app.bitset_fiber_bytes / 1024.0);
  printf("  Consistency Audit: %s%u/%u CHECKS %s\033[0m (Max Residual: %.2e | Gen: %llu)\n",
         app.all_checks_passed ? "\033[1;32m" : "\033[1;31m",
         app.checks_passed, app.checks_total,
         app.all_checks_passed ? "PASS" : "DEGRADED",
         app.max_numerical_residual, (unsigned long long)app.index_generation);
  printf("------------------------------------------------------------------------\n");
  printf("  \033[1;35m[Finite Poset, Ideal Algebra & Numerical Diagnostics]\033[0m\n");
  printf("  Leinster Magnitude |C|_L : %8.4f   Euler Char chi(N(C)) : %d\n",
         sheaf.leinster_magnitude, sheaf.euler_characteristic);
  printf("  Fiedler Connectivity l_2 : %8.4f   Cochain ||d1*d0||_2  : %.2e\n",
         sheaf.fiedler_eigenvalue, sheaf.cochain_exactness_norm);
  printf("  Hodge Gradient ||d0*phi||: %8.2f   Hodge Solenoidal Curl: %8.2f\n",
         sheaf.hodge_gradient_norm, sheaf.hodge_solenoidal_norm);
  printf("  Hodge Coexact ||d1^T*psi|: %8.2f   Hodge Harmonic Resid : %8.2f\n",
         sheaf.hodge_coexact_norm, sheaf.hodge_harmonic_norm);
  printf("  1-Laplacian Trace Tr(L1) : %8.2f   Isbell Hull Defect   : %.2e\n",
         sheaf.laplacian_1_trace, topos.isbell_envelope_defect);
  printf("  Weighted Adjunction Resid: %.2e   Ideal Boundary Check : %s\n",
         topos.tensor_hom_adjunction_defect, topos.leibniz_rule_holds ? "PASS" : "FAIL");
  printf("  Cocone Adjunction Resid  : %.2e   Modal Closure j(M)=j(A): %s\n",
         topos.cocone_adjunction_defect, topos.modal_closure_matches ? "MATCH" : "MISMATCH");
  printf("  Sinkhorn W_eps / S_eps   : %.3f/%.3f  Fisher-Rao Geodesic  : %8.4f\n",
         topos.sinkhorn_ot.entropic_wasserstein,
         topos.sinkhorn_ot.sinkhorn_divergence,
         topos.sinkhorn_ot.fisher_rao_geodesic);
  printf("  Cheeger Const h(C)       : %8.4f   Cheeger-Buser Bound  : %s\n",
         topos.sinkhorn_ot.cheeger_constant,
         topos.sinkhorn_ot.cheeger_buser_holds ? "HOLDS" : "FAIL");
  printf("  Rank-Page Euler Summary  : %d/%d/%d/%d    Page d_r^2 Residual : %.2e\n",
         topos.rank_page_diagnostics.euler_e0,
         topos.rank_page_diagnostics.euler_e1,
         topos.rank_page_diagnostics.euler_e2,
         topos.rank_page_diagnostics.euler_e_inf,
         topos.rank_page_diagnostics.differential_nilpotency_defect);
  printf("  Gauss-Bonnet sum K_GB    : %8.6f   Forman/Ollivier Curv : %.2f / %.3f\n",
         topos.ricci_curv.gauss_bonnet_total,
         topos.ricci_curv.forman_mean_curvature,
         topos.ricci_curv.ollivier_mean_curvature);
  printf("  Ihara Zeta zeta_C(0.15)  : %8.4f   Tropical Idem Defect : %.2e\n",
         topos.zeta_tropical.ihara_zeta_val,
         topos.zeta_tropical.tropical_idempotence_defect);
  printf("  Rota-Baxter Id Defect    : %.2e   Atkinson Birkhoff Err: %.2e\n",
         topos.rota_baxter.rota_baxter_identity_defect,
         topos.rota_baxter.spitzer_birkhoff_defect);
  printf("  Kleisli Monad Entropy    : %8.4f   Zig-Zag Defect       : %.2e\n",
         monad.kleisli_entropy, monad.zig_zag_triangle_defect);
  printf("  Temporal Sheaf Laplacian : %8.4f   Emerging Fiber       : %s\n",
         temp.connection_laplacian_norm, ontology_node(temp.fastest_emerging_node)->slug);
  printf("------------------------------------------------------------------------\n");
  printf("  \033[1;33m[Atmospheric Physics (Lorenz-63 Chaos) & Autonomous Watchpoints]\033[0m\n");
  printf("  Lorenz-63 Max Lyapunov   : %8.4f   Kaplan-Yorke Dim D_KY: %8.4f\n",
         wx_phys.lyapunov_max, wx_phys.kaplan_yorke_dim);
  printf("  Clausius-Clapeyron e_s   : %6.2f hPa  Stull Wet-Bulb T_w   : %6.2f C\n",
         wx_phys.saturation_vapor_hpa, wx_phys.wet_bulb_celsius);
  printf("  Rossby Stationary K_s    : %8.4f   Automation Rules/Hits: %zu / %zu\n",
         wx_phys.rossby_stationary_k, rt->eng.automation_rule_count, rt->eng.automation_alert_count);
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
  return 0;
}

typedef int (*CliCommandHandler)(CliRuntime *rt, int argc, char **argv);

typedef struct {
  const char *name;
  CliCommandHandler handler;
} CliCommand;

static const CliCommand k_cli_commands[] = {
  {"seed", cmd_seed},
  {"poll", cmd_poll},
  {"search", cmd_search},
  {"extract", cmd_extract},
  {"tokenize", cmd_tokenize},
  {"api", cmd_api},
  {"query", cmd_api},
  {"automate", cmd_automate},
  {"watch", cmd_automate},
  {"weather", cmd_weather},
  {"stats", cmd_stats},
  {"status", cmd_stats},
  {"appliance", cmd_stats},
  {"machine", cmd_stats},
  {"tui", cmd_tui},
  {"top", cmd_tui}
};

static const CliCommand *find_cli_command(const char *name) {
  for(size_t i = 0; i < sizeof(k_cli_commands) / sizeof(k_cli_commands[0]); ++i) {
    if(strcmp(k_cli_commands[i].name, name) == 0) return &k_cli_commands[i];
  }
  return NULL;
}

int main(int argc, char **argv) {
  const char *cmd = argc > 1 ? argv[1] : "serve";
  if(strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
    print_usage(stdout);
    return 0;
  }
  if(strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
    printf("oyez %s\n", OYEZ_VERSION_STRING);
    return 0;
  }
  if(strcmp(cmd, "bench") == 0) {
    return run_bench();
  }
  if(strcmp(cmd, "serve") == 0) {
    return run_serve(argc, argv);
  }

  const CliCommand *entry = find_cli_command(cmd);
  if(!entry) {
    fprintf(stderr, "Unknown command: %s\n\n", cmd);
    print_usage(stderr);
    return 2;
  }

  CliRuntime rt;
  cli_runtime_init(&rt);
  int rc = entry->handler(&rt, argc, argv);
  cli_runtime_free(&rt);
  return rc;
}
