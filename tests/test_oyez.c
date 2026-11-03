#include "catalog.h"
#include "checksum.h"
#include "core.h"
#include "http_server.h"
#include "index_engine.h"
#include "m1_tokenizer.h"
#include "markup.h"
#include "ontology.h"
#include "token.h"
#include "wire.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_checksum_and_wire(void) {
  const char *msg = "oyez-c23-wire-test";
  uint32_t crc = checksum_crc32(msg, strlen(msg));
  assert(crc != 0u);

  char hex[65];
  sha256_hex(msg, strlen(msg), hex);
  assert(sha256_valid_hex(hex) == 1);

  Buffer buf;
  buffer_init(&buf);
  wire_append_u32(&buf, 0x12345678u);
  wire_append_u64(&buf, 0xCAFEBABE12345678ULL);
  wire_append_f64(&buf, 3.1415926535);
  size_t off = 0;
  uint32_t u32 = 0;
  uint64_t u64 = 0;
  double v = 0.0;
  assert(wire_read_u32((const unsigned char *)buf.data, buf.len, &off, &u32) && u32 == 0x12345678u);
  assert(wire_read_u64((const unsigned char *)buf.data, buf.len, &off, &u64) && u64 == 0xCAFEBABE12345678ULL);
  assert(wire_read_f64((const unsigned char *)buf.data, buf.len, &off, &v));
  assert(v > 3.1415 && v < 3.1416);
  buffer_free(&buf);
}

static void test_bpe_and_m1_tokenizer(void) {
  const char *corpus =
      "typedef struct OyezEngine {\n"
      "    uint32_t vocab_size;\n"
      "    return sizeof(uint32_t);\n"
      "} OyezEngine;\n"
      "diff --git a/src/core.c b/src/core.c\n"
      "error: undefined reference to symbol\n"
      "SELECT * FROM articles WHERE region = 'east-asia' ORDER BY score;\n";

  Tokenizer bpe;
  memset(&bpe, 0, sizeof(bpe));
  TokenTrainConfig cfg = token_train_config_default();
  cfg.target_vocab = 300u;
  cfg.min_frequency = 2u;
  char err[256];
  assert(token_train(&bpe, corpus, strlen(corpus), NULL, 0, NULL, 0, &cfg, err));
  assert(bpe.vocab > 256u);

  uint32_t *ids = NULL;
  size_t count = 0;
  assert(token_encode(&bpe, corpus, strlen(corpus), &ids, &count, err));
  assert(count > 0 && count < strlen(corpus));

  Buffer dec_buf;
  buffer_init(&dec_buf);
  assert(token_decode(&bpe, ids, count, &dec_buf, err));
  assert(dec_buf.len == strlen(corpus));
  assert(memcmp(dec_buf.data, corpus, dec_buf.len) == 0);
  free(ids);
  buffer_free(&dec_buf);

  Buffer saved;
  buffer_init(&saved);
  assert(token_save(&bpe, &saved));
  Tokenizer bpe_loaded;
  memset(&bpe_loaded, 0, sizeof(bpe_loaded));
  assert(token_load(&bpe_loaded, saved.data, saved.len, err));
  assert(bpe_loaded.vocab == bpe.vocab);
  buffer_free(&saved);
  token_free(&bpe);
  token_free(&bpe_loaded);

  M1Tokenizer m1;
  m1_tokenizer_init(&m1);
  assert(m1_tokenizer_train(&m1, (const uint8_t *)corpus, strlen(corpus), M1_FIXED_VOCAB_SIZE + 32u) == 0);
  assert(m1_tokenizer_vocab_size(&m1) > M1_FIXED_VOCAB_SIZE);

  uint32_t *m1_ids = NULL;
  size_t m1_count = 0;
  assert(m1_tokenizer_encode(&m1, (const uint8_t *)corpus, strlen(corpus), &m1_ids, &m1_count) == 0);
  assert(m1_count > 0);

  uint8_t *m1_dec = NULL;
  size_t m1_dlen = 0;
  assert(m1_tokenizer_decode(&m1, m1_ids, m1_count, &m1_dec, &m1_dlen) == 0);
  assert(m1_dlen == strlen(corpus));
  assert(memcmp(m1_dec, corpus, m1_dlen) == 0);
  free(m1_ids);
  free(m1_dec);

  const char *m1_path = "/tmp/test_oyez_m1.bin";
  assert(m1_tokenizer_save(&m1, m1_path) == 0);
  M1Tokenizer m1_loaded;
  m1_tokenizer_init(&m1_loaded);
  assert(m1_tokenizer_load(&m1_loaded, m1_path) == 0);
  assert(strcmp(m1.sha256, m1_loaded.sha256) == 0);
  unlink(m1_path);

  M1Sequence seq;
  m1_sequence_init(&seq);
  assert(m1_sequence_begin(&seq) == 0);
  assert(m1_sequence_message(&seq, &m1, M1_CTRL_USER, "Summarize dispatch", 18u, 0u) == 0);
  assert(m1_sequence_assistant(&seq, &m1, "Checking poset", 14u, "Done.", 5u, 0u) == 0);
  assert(m1_sequence_end(&seq) == 0);
  assert(seq.length > 6u);
  assert(seq.tokens[0] == M1_CTRL_BOS);
  assert(seq.tokens[seq.length - 1u] == M1_CTRL_EOS);
  m1_sequence_free(&seq);
  m1_tokenizer_free(&m1);
  m1_tokenizer_free(&m1_loaded);
}

static void test_markup_and_readability(void) {
  const char *rss =
      "<?xml version=\"1.0\"?>\n"
      "<rss version=\"2.0\"><channel>\n"
      "<title>Global Wire &amp; Dispatch</title>\n"
      "<link>https://wire.example.org</link>\n"
      "<description>World news</description>\n"
      "<item>\n"
      "<guid>item-101</guid>\n"
      "<title>BREAKING: TSMC expands wafer fab in Tokyo &amp; Kumamoto</title>\n"
      "<link>https://wire.example.org/101</link>\n"
      "<description><![CDATA[<p>Semiconductor supply chain in <b>Japan</b> accelerates.</p>]]></description>\n"
      "<pubDate>Tue, 03 Nov 2026 04:00:00 GMT</pubDate>\n"
      "<media:content url=\"https://wire.example.org/img/tsmc.jpg\" medium=\"image\"/>\n"
      "<category>technology</category>\n"
      "</item>\n"
      "</channel></rss>";

  ParsedFeed feed;
  assert(feed_parse(rss, strlen(rss), "https://wire.example.org", &feed) == 0);
  assert(feed.format == FEED_FORMAT_RSS2);
  assert(strcmp(feed.title, "Global Wire & Dispatch") == 0);
  assert(feed.item_count == 1u);
  assert(strcmp(feed.items[0].guid, "item-101") == 0);
  assert(strstr(feed.items[0].summary, "Semiconductor supply chain in Japan") != NULL);
  assert(strcmp(feed.items[0].image_url, "https://wire.example.org/img/tsmc.jpg") == 0);
  uint64_t ts_rfc = parse_timestamp_ms(feed.items[0].published_at);
  uint64_t ts_iso = parse_timestamp_ms("2026-11-03T04:00:00Z");
  assert(ts_rfc > 1700000000000ULL && ts_rfc == ts_iso);
  feed_free(&feed);

  const char *html =
      "<html><head><meta property=\"og:title\" content=\"Quantum Cryptography Breakthrough\"/>"
      "<meta name=\"author\" content=\"Dr. Ada Lovelace\"/></head>"
      "<body><nav>Menu</nav><article><h2>Lattice Key Exchange</h2>"
      "<p>Researchers in Geneva and Zurich demonstrated post-quantum encryption over optical fiber, "
      "achieving unprecedented key rates across metropolitan networks.</p>"
      "<p>The peer-reviewed study was published alongside European Union cybersecurity guidelines.</p>"
      "</article></body></html>";

  ExtractedContent ex;
  assert(readability_extract(html, strlen(html), "https://science.example.org/qkd", &ex) == 0);
  assert(strcmp(ex.title, "Quantum Cryptography Breakthrough") == 0);
  assert(strcmp(ex.byline, "Dr. Ada Lovelace") == 0);
  assert(strstr(ex.text_content, "post-quantum encryption") != NULL);
  assert(strstr(ex.markdown, "## Lattice Key Exchange") != NULL);
  assert(ex.word_count >= 20u);
  readability_free(&ex);
}

static void test_category_theory_and_ontology(void) {
  ontology_init();
  /* Reflexivity: c <= c */
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    assert(ontology_has_morphism((OntologyNodeId)i, (OntologyNodeId)i) == 1);
    assert(ontology_has_morphism((OntologyNodeId)i, ONTO_ROOT) == 1);
  }
  /* Transitivity: cryptography <= mathematics <= ai <= technology <= knowledge-economy <= all-news */
  assert(ontology_has_morphism(ONTO_CRYPTOGRAPHY, ONTO_MATHEMATICS) == 1);
  assert(ontology_has_morphism(ONTO_MATHEMATICS, ONTO_AI) == 1);
  assert(ontology_has_morphism(ONTO_AI, ONTO_TECHNOLOGY) == 1);
  assert(ontology_has_morphism(ONTO_TECHNOLOGY, ONTO_KNOWLEDGE_ECONOMY) == 1);
  assert(ontology_has_morphism(ONTO_CRYPTOGRAPHY, ONTO_KNOWLEDGE_ECONOMY) == 1);

  /* Join & Meet: cyber <= security and cyber <= technology */
  uint32_t meet = ontology_meet_mask(ONTO_SECURITY, ONTO_TECHNOLOGY);
  assert((meet & (1u << ONTO_CYBER)) != 0u);
  OntologyNodeId join = ontology_join(ONTO_AI, ONTO_SPACE);
  assert(join == ONTO_SCIENCE || join == ONTO_TECHNOLOGY || join == ONTO_KNOWLEDGE_ECONOMY);

  /* Heyting Algebra Implication (A => B) & Intuitionistic Negation (~A) */
  uint32_t h_imp = ontology_heyting_implication(1u << ONTO_AI, 1u << ONTO_TECHNOLOGY);
  /* Since AI <= TECHNOLOGY, (AI => TECHNOLOGY) is the entire 32-node poset (0xFFFFFFFFu) */
  assert(h_imp == 0xFFFFFFFFu);
  uint32_t h_neg = ontology_heyting_negation(1u << ONTO_SPORTS);
  /* AI is disjoint from SPORTS in upward closure, so AI is in ~SPORTS */
  assert((h_neg & (1u << ONTO_AI)) != 0u);
  assert((h_neg & (1u << ONTO_SPORTS)) == 0u);

  /* Left & Right Kan Extensions + Rota Mobius Inversion + Sheaf Cohomology */
  double raw[ONTOLOGY_NODE_COUNT] = {0};
  double kan[ONTOLOGY_NODE_COUNT] = {0};
  double ran[ONTOLOGY_NODE_COUNT] = {0};
  double mobius_rec[ONTOLOGY_NODE_COUNT] = {0};
  raw[ONTO_AI] = 10.0;
  raw[ONTO_CYBER] = 6.0;
  raw[ONTO_QUANTUM] = 8.0;
  raw[ONTO_CRYPTOGRAPHY] = 5.0;
  ontology_left_kan_extend(raw, kan);
  ontology_right_kan_extend(kan, ran);
  ontology_mobius_invert(kan, mobius_rec);
  /* Mobius inversion over the incidence algebra must exactly recover the raw signal f(c) */
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double diff = mobius_rec[c] > raw[c] ? (mobius_rec[c] - raw[c]) : (raw[c] - mobius_rec[c]);
    assert(diff < 1e-9);
  }
  assert(kan[ONTO_TECHNOLOGY] >= 24.0);
  assert(kan[ONTO_SECURITY] >= 6.0);
  assert(kan[ONTO_ROOT] >= kan[ONTO_TECHNOLOGY]);
  assert(ran[ONTO_AI] > 0.0);

  /* Wasserstein / Radon metric on poset filters */
  assert(ontology_wasserstein_distance(1u << ONTO_AI, 1u << ONTO_AI) < 1e-9);
  double d_ai_rob = ontology_wasserstein_distance(1u << ONTO_AI, 1u << ONTO_ROBOTICS);
  double d_ai_spo = ontology_wasserstein_distance(1u << ONTO_AI, 1u << ONTO_SPORTS);
  assert(d_ai_rob < d_ai_spo);

  SheafCohomology sheaf;
  ontology_sheaf_cohomology(raw, kan, &sheaf);
  assert(sheaf.sheaf_section[ONTO_TECHNOLOGY] >= 24.0);
  /* Simplicial Nerve N(C) of a poset with terminal object (ONTO_ROOT) is contractible:
   * Euler characteristic chi(N(C)) = f_0 - f_1 + f_2 - f_3 must equal 1 */
  assert(sheaf.nerve_f_vector[0] == 32u);
  assert(sheaf.nerve_f_vector[1] > 60u);
  assert(sheaf.euler_characteristic == 1);
  assert(sheaf.betti_numbers[0] == 1u && sheaf.betti_numbers[1] == 0u);
  assert(sheaf.mobius_total_variation > 60u);
  assert(sheaf.laplacian_trace > 40.0);
  assert(sheaf.spectral_radius > 4.0);
  assert(sheaf.fiedler_eigenvalue > 0.2);
  assert(sheaf.dirichlet_energy > 0.0);
  /* Cochain complex exactness: \delta_1(\delta_0 f) = 0 on every 2-simplex */
  assert(sheaf.cochain_exactness_norm < 1e-12);
  assert(sheaf.hodge_1_curl_norm >= 0.0);

  /* Lawvere Enriched Quasimetric Space:
   * 1. Zero self-distance d_L(A, A) = 0
   * 2. Directed asymmetry: since AI -> TECHNOLOGY is an upward morphism (cost 0.5)
   *    and TECHNOLOGY -> AI is a downward specialization (cost 1.5),
   *    d_L(AI, TECHNOLOGY) < d_L(TECHNOLOGY, AI).
   * 3. Directed Triangle Inequality: d_L(A, C) <= d_L(A, B) + d_L(B, C). */
  assert(ontology_lawvere_distance(1u << ONTO_AI, 1u << ONTO_AI) < 1e-9);
  double dl_up = ontology_lawvere_distance(1u << ONTO_AI, 1u << ONTO_TECHNOLOGY);
  double dl_down = ontology_lawvere_distance(1u << ONTO_TECHNOLOGY, 1u << ONTO_AI);
  assert(dl_up < dl_down);
  double dl_ac = ontology_lawvere_distance(1u << ONTO_CRYPTOGRAPHY, 1u << ONTO_SPORTS);
  double dl_ab = ontology_lawvere_distance(1u << ONTO_CRYPTOGRAPHY, 1u << ONTO_TECHNOLOGY);
  double dl_bc = ontology_lawvere_distance(1u << ONTO_TECHNOLOGY, 1u << ONTO_SPORTS);
  assert(dl_ac <= dl_ab + dl_bc + 1e-9);

  /* Lawvere-Tierney Double-Negation Topology j(A) = ~~A on Heyting ideals:
   * 1. Inflationary: A is a subset of j(A)
   * 2. Idempotent: j(j(A)) == j(A)
   * 3. Meet-preserving on ideals: j(A & B) == j(A) & j(B) */
  uint32_t sub_a = ontology_downward_closure((1u << ONTO_AI) | (1u << ONTO_QUANTUM));
  uint32_t sub_b = ontology_downward_closure((1u << ONTO_AI) | (1u << ONTO_CYBER));
  uint32_t ja = ontology_lawvere_tierney_sheafify(sub_a);
  uint32_t jb = ontology_lawvere_tierney_sheafify(sub_b);
  uint32_t j_ja = ontology_lawvere_tierney_sheafify(ja);
  uint32_t j_ab = ontology_lawvere_tierney_sheafify(sub_a & sub_b);
  assert((ja & sub_a) == sub_a);
  assert(j_ja == ja);
  assert(j_ab == (ja & jb));
  assert(ontology_is_grothendieck_dense(0xFFFFFFFFu) == 1);

  /* Profunctor Coend \int^{c \in C} F(c) G(c) via Rota Mobius kernel */
  double coend_val = ontology_profunctor_coend(kan, kan);
  assert(coend_val > 0.0);
  assert(ontology_mobius_value(ONTO_AI, ONTO_AI) == 1);
  assert(ontology_mobius_value(ONTO_AI, ONTO_TECHNOLOGY) == -1);
  assert(ontology_information_specificity(1u << ONTO_CRYPTOGRAPHY) >
         ontology_information_specificity(1u << ONTO_TECHNOLOGY));

  /* Leinster Magnitude |C|_L, Constructive Yoneda Lemma Nat(Y(c), F) ~= F(c),
   * Enriched Lawvere-Yoneda Kan Transform, and Helmholtz-Hodge Decomposition */
  double mag = ontology_leinster_magnitude();
  assert(mag > 5.0 && mag < 32.0);
  assert(ontology_node_magnitude_weight(ONTO_AI) > 0.0);
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double yon = ontology_yoneda_lemma_eval((OntologyNodeId)c, kan);
    double ydiff = yon > kan[c] ? (yon - kan[c]) : (kan[c] - yon);
    assert(ydiff < 1e-9);
  }
  double ykan[ONTOLOGY_NODE_COUNT] = {0};
  ontology_yoneda_kan_transform(raw, ykan);
  assert(ykan[ONTO_AI] >= 10.0 - 1e-9);
  assert(ykan[ONTO_TECHNOLOGY] > 0.0);
  assert(sheaf.leinster_magnitude > 5.0);
  assert(sheaf.hodge_gradient_norm > 0.0);
  assert(sheaf.hodge_solenoidal_norm >= 0.0);

  /* Persistent Homology Barcode & Adjunction Monad/Comonad Triangle Identities */
  PersistenceBarcode barcode;
  ontology_persistent_homology(raw, &barcode);
  assert(barcode.total_persistence_l1 > 0.5);
  assert(barcode.steps[PERSISTENCE_FILTRATION_STEPS - 1].f_vector[0] == 32u);
  assert(barcode.steps[PERSISTENCE_FILTRATION_STEPS - 1].betti_0 == 1u);

  AdjunctionMonadState monad;
  ontology_adjunction_monad(raw, &monad);
  assert(monad.zig_zag_triangle_defect < 1e-12);
  assert(monad.kleisli_entropy > 0.5);
  assert(monad.eilenberg_moore_mask != 0u);

  uint32_t m1_landmarks = 0;
  uint64_t m1_sk = m1_structural_sketch64("DeepMind AlphaFold FlashAttention CVE-2026-1337 FOMC", 52u, &m1_landmarks);
  assert(m1_sk != 0ULL);
  assert(m1_landmarks >= 2u);

  /* Epistemic NewsType classification */
  assert(news_type_classify("BREAKING: Earthquake strikes Pacific trench", "", "", "climate") ==
         NEWS_TYPE_BREAKING_DISPATCH);
  assert(news_type_classify("Leaked documents uncovered in whistleblower investigation", "", "", "law") ==
         NEWS_TYPE_INVESTIGATIVE_REPORT);
  assert(news_type_classify("Central bank raises interest rate by 50 basis points", "", "", "economy") ==
         NEWS_TYPE_MARKET_AND_ECONOMIC);

  /* CVE, OPSEC & High-Clearance extraction */
  ExtractedEntities sec_ent;
  entities_extract("Zero-day exploit chain in V8 WebAssembly sandbox (CVE-2026-1337 and CVE-2026-9001)",
                   "Cloudflare Turnstile JA4 TLS fingerprint and DARPA SIGINT mitigation.",
                   "Post-quantum lattice cryptography and zero-knowledge proofs.",
                   "cyber",
                   NULL,
                   0,
                   &sec_ent);
  assert(sec_ent.cve_count == 2u);
  assert(strcmp(sec_ent.cves[0], "CVE-2026-1337") == 0);
  assert(strcmp(sec_ent.cves[1], "CVE-2026-9001") == 0);
  assert(sec_ent.clearance == CLEARANCE_STRATEGIC);
  assert(sec_ent.opsec_score > 10.0);
  assert((sec_ent.direct_ontology_mask & (1u << ONTO_CRYPTOGRAPHY)) != 0u);

  /* Macro, FX, Equities & Fiscal Event extraction */
  ExtractedEntities fin_ent;
  entities_extract("Federal Reserve announces hawkish rate hike of 50 basis points as S&P 500 and Japanese Yen react",
                   "Treasury auction and CPI core inflation print push 10-year yield higher.",
                   "U.S. dollar (DXY) and Brent crude oil trade alongside nonfarm payrolls.",
                   "finance",
                   NULL,
                   0,
                   &fin_ent);
  assert(fin_ent.fiscal_stance == FISCAL_HAWKISH);
  assert(fin_ent.asset_count >= 4u);
  assert(fin_ent.fiscal_event_count >= 3u);
  assert(fin_ent.macro_score > 10.0);

  /* AI Foundation Models, Neoclouds, Inference Stack & Compute Tier extraction */
  ExtractedEntities ai_ent;
  entities_extract("CoreWeave deploys NVIDIA Blackwell GB200 NVL72 clusters running DeepSeek-R1 and Llama-3 with vLLM and SGLang",
                   "FlashAttention-3 FP8 quantization and speculative decoding accelerate GRPO reinforcement learning test-time compute.",
                   "Together AI, Fireworks AI, and Groq LPU inference clusters benchmark KV-cache disaggregation over InfiniBand.",
                   "ai",
                   NULL,
                   0,
                   &ai_ent);
  assert(ai_ent.ai_tier == AI_TIER_FRONTIER_PRETRAINING);
  assert(ai_ent.ai_model_count >= 2u);
  assert(ai_ent.ai_infra_count >= 5u);
  assert(ai_ent.ai_compute_score > 20.0);
  assert(ai_ent.ai_license == AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE);
  assert(ai_ent.openness_score > 0.0);

  /* Hugging Face Hub <org>/<model> repo ID & Open-Science vs Closed-Frontier license extraction */
  ExtractedEntities hf_ent;
  entities_extract("AllenAI releases allenai/OLMo-2-1124-13B alongside deepseek-ai/DeepSeek-R1-Distill-Qwen-32B on Hugging Face Hub",
                   "Open-source OSI AI with reproducible pretraining data on FineWeb and Dolma outpaces closed-source API frontier models.",
                   "Developers quantize meta-llama/Llama-3.3-70B-Instruct and Qwen/Qwen2.5-Coder-32B-Instruct into GGUF with Unsloth.",
                   "ai",
                   NULL,
                   0,
                   &hf_ent);
  assert(hf_ent.hf_repo_count >= 4u);
  assert(strcmp(hf_ent.hf_repos[0], "deepseek-ai/deepseek-r1-distill-qwen-32b") == 0);
  assert(strcmp(hf_ent.hf_repos[1], "meta-llama/llama-3.3-70b-instruct") == 0);
  assert(strcmp(hf_ent.hf_repos[2], "qwen/qwen2.5-coder-32b-instruct") == 0);
  assert(strcmp(hf_ent.hf_repos[3], "allenai/olmo-2-1124-13b") == 0);
  assert(hf_ent.ai_license == AI_LICENSE_OPEN_SCIENCE_OSI);
  assert(hf_ent.openness_score > 15.0);

  /* Google DeepMind Research, Politics/Law & Global Sports extraction */
  ExtractedEntities dm_sp_ent;
  entities_extract("Google DeepMind unveils AlphaFold 3, AlphaProof and AlphaGeometry 2 trained with JAX on Trillium TPU",
                   "Supreme Court antitrust ruling and general election referendum shape Champions League and Formula 1 broadcast rights.",
                   "Isomorphic Labs and MuZero researchers analyze Premier League and NBA telemetry.",
                   "science",
                   NULL,
                   0,
                   &dm_sp_ent);
  assert(dm_sp_ent.ai_tier == AI_TIER_FRONTIER_PRETRAINING);
  assert(dm_sp_ent.ai_model_count >= 3u);
  assert(dm_sp_ent.ai_infra_count >= 2u);
  assert(dm_sp_ent.fiscal_event_count >= 2u);
  assert(dm_sp_ent.sport_count >= 4u);
  assert(dm_sp_ent.sports_score > 10.0);
  assert((dm_sp_ent.direct_ontology_mask & (1u << ONTO_SPORTS)) != 0u);
}

static void test_engine_search_and_categorical_endpoints(void) {
  OyezEngine eng;
  engine_init(&eng);

  size_t seeded = engine_seed_catalog(&eng, NULL, NULL);
  assert(seeded >= 640u);

  uint32_t f_tw = 0, f_eu = 0;
  CatalogEntry ce_tw = {"Focus Taiwan", "https://focustaiwan.tw/RSS", "https://focustaiwan.tw",
                        "technology", REGION_EAST_ASIA, "TW", "Taiwan", "en", "wire", "Tech"};
  CatalogEntry ce_eu = {"EUobserver", "https://euobserver.com/rss.xml", "https://euobserver.com",
                        "politics", REGION_EUROPE, "BE", "EU", "en", "specialist", "EU"};
  engine_upsert_feed(&eng, &ce_tw, &f_tw);
  engine_upsert_feed(&eng, &ce_eu, &f_eu);

  int ins = 0;
  uint32_t a1 = 0, a2 = 0, a3 = 0;
  engine_ingest_article(&eng,
                        f_tw,
                        "tw-1",
                        "TSMC unveils next-gen artificial intelligence semiconductor wafer in Taipei",
                        "https://focustaiwan.tw/1",
                        "Desk",
                        "New neural network chip architecture announced in Taiwan and Japan.",
                        "<p>Artificial intelligence semiconductor manufacturing in Taipei and Tokyo.</p>",
                        "2026-11-03T01:00:00Z",
                        1762130000000ULL,
                        "",
                        NULL,
                        0,
                        &a1,
                        &ins);
  assert(ins == 1);

  engine_ingest_article(&eng,
                        f_tw,
                        "tw-2",
                        "TSMC unveils next-gen artificial intelligence semiconductor wafer in Taipei facility",
                        "https://focustaiwan.tw/2",
                        "Desk",
                        "New neural network chip architecture announced in Taiwan and Japan today.",
                        "<p>Artificial intelligence semiconductor manufacturing in Taipei and Tokyo.</p>",
                        "2026-11-03T01:05:00Z",
                        1762130100000ULL,
                        "",
                        NULL,
                        0,
                        &a2,
                        &ins);
  assert(ins == 1);

  engine_ingest_article(&eng,
                        f_eu,
                        "eu-1",
                        "European Union parliament debates artificial intelligence semiconductor regulation in Brussels",
                        "https://euobserver.com/1",
                        "Desk",
                        "Brussels lawmakers review semiconductor supply chain and Taiwan trade.",
                        "<p>European Union legislation on artificial intelligence and microchip imports from Taiwan.</p>",
                        "2026-11-03T02:00:00Z",
                        1762130200000ULL,
                        "",
                        NULL,
                        0,
                        &a3,
                        &ins);
  assert(ins == 1);

  /* Presheaf restriction: querying super-category 'knowledge-economy' retrieves 'ai'/'technology' articles */
  ArticleQuery aq;
  memset(&aq, 0, sizeof(aq));
  aq.query = "semiconductor";
  aq.category = "knowledge-economy";
  aq.use_category_morphism = 1;
  aq.diversify_mmr = 1;
  aq.mmr_lambda = 0.6;
  aq.limit = 10u;

  SearchResponse res;
  assert(engine_search(&eng, &aq, &res) == 0);
  assert(res.total == 3u);
  search_response_free(&res);

  /* Domain alias expansion: 'AI' in article matches 'artificial-intelligence' or 'ai' query,
   * and organization BitSet fiber filters by 'EU' */
  ArticleQuery org_q;
  memset(&org_q, 0, sizeof(org_q));
  org_q.organization = "EU";
  org_q.limit = 10u;
  assert(engine_search(&eng, &org_q, &res) == 0);
  assert(res.total == 1u);
  assert(res.hits[0].article->id == a3);
  search_response_free(&res);

  /* SimHash64 near-duplicate detection between a1 and a2 */
  const ArticleRecord *dups[8];
  uint32_t dists[8];
  size_t nd = engine_find_near_duplicates(&eng, a1, 16u, dups, dists, 8u);
  assert(nd >= 1u);
  assert(dups[0]->id == a2);

  /* Categorical pullback between east-asia and europe over shared topic / mentioned_country */
  PullbackPair pairs[16];
  size_t np = engine_categorical_pullback(&eng, "east-asia", "europe", "mentioned_country", pairs, 16u);
  assert(np >= 1u);
  assert(strcmp(pairs[0].shared_witness, "TW") == 0);

  /* Categorical pushout colimit X \sqcup_Z Y & Presheaf Natural Transformation \eta: F => G */
  PushoutAmalgam amalgams[16];
  size_t nam = engine_categorical_pushout(&eng, "east-asia", "europe", "mentioned_country", amalgams, 16u);
  assert(nam >= 1u);
  assert(strcmp(amalgams[0].shared_witness, "TW") == 0);
  assert(amalgams[0].member_count >= 2u);
  assert(amalgams[0].left_count >= 1u && amalgams[0].right_count >= 1u);
  assert(amalgams[0].colimit_cocone_score > 0.0);

  PresheafMorphism nat_morph;
  engine_natural_transformation(&eng, REGION_EAST_ASIA, REGION_EUROPE, &nat_morph);
  assert(nat_morph.coend_pairing > 0.0);
  assert(nat_morph.naturality_defect >= 0.0);
  assert(nat_morph.heyting_truth_mask != 0u);

  /* Formal Concept Analysis + Galois Hasse Diagram */
  FormalConcept concepts[16];
  size_t nc = engine_formal_concepts(&eng, 1u, concepts, 16u);
  assert(nc >= 2u);
  assert(concepts[0].lawvere_tierney_mask != 0u);
  assert(concepts[0].intensional_entropy > 0.0);

  /* Heyting / BitSet Difference Filtering (?exclude_category=politics) */
  ArticleQuery ex_q;
  memset(&ex_q, 0, sizeof(ex_q));
  ex_q.category = "all-news";
  ex_q.exclude_category = "politics";
  ex_q.use_category_morphism = 1;
  ex_q.limit = 10u;
  assert(engine_search(&eng, &ex_q, &res) == 0);
  assert(res.total == 2u);
  search_response_free(&res);

  /* Bipartite PageRank */
  RankedEntity ents[16];
  ScoredArticle top_a[8];
  size_t ne = engine_bipartite_pagerank(&eng, 12u, 0.85, ents, 16u, top_a, 8u);
  assert(ne >= 2u);

  /* Multi-Probe LSH Story Clustering */
  StoryCluster clusters[16];
  size_t ncl = engine_story_clusters(&eng, 18u, clusters, 16u);
  assert(ncl >= 1u);

  /* Snapshot persistence round-trip */
  const char *snap = "/tmp/test_oyez_snap.ix";
  assert(engine_save_snapshot(&eng, snap) == 0);
  OyezEngine loaded;
  engine_init(&loaded);
  assert(engine_load_snapshot(&loaded, snap) == 0);
  assert(loaded.feed_count == eng.feed_count);
  assert(loaded.article_count == eng.article_count);
  unlink(snap);

  /* HTTP Handler test */
  M1Tokenizer tok;
  m1_tokenizer_init(&tok);
  ServerContext ctx = {&eng, &tok, NULL, 8787u};
  int status = 0;
  Buffer json;
  assert(http_handle_request(&ctx, "GET", "/api/v1/ontology?sub=ai&sup=knowledge-economy&a=cyber&b=ai", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"exists\":true") != NULL);
  assert(strstr(json.data, "\"euler_characteristic\":1") != NULL);
  assert(strstr(json.data, "\"fiedler_eigenvalue\":") != NULL);
  assert(strstr(json.data, "\"leinster_magnitude\":") != NULL);
  assert(strstr(json.data, "\"hodge_gradient_norm\":") != NULL);
  assert(strstr(json.data, "\"yoneda_kan_score\":") != NULL);
  assert(strstr(json.data, "\"lawvere_distance_ab\":") != NULL);
  assert(strstr(json.data, "\"lawvere_tierney_sheaf_a\":") != NULL);
  buffer_free(&json);

  CartesianLift lifts[16];
  size_t n_lifts = engine_grothendieck_fibration(&eng, "all-news", lifts, 16u);
  assert(n_lifts >= 1u);
  assert(lifts[0].cartesian_fidelity > 0.0);

  assert(http_handle_request(&ctx, "GET", "/api/v1/fibration?category=all-news", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"cartesian_lifts\":") != NULL);
  assert(strstr(json.data, "\"leinster_magnitude\":") != NULL);
  buffer_free(&json);

  TemporalSheafState temp_sh;
  engine_temporal_sheaf(&eng, &temp_sh);
  assert(temp_sh.connection_laplacian_norm > 0.0);

  OperadChain op_chains[8];
  size_t n_op = engine_operad_compose(&eng, "east-asia", "europe", "east-asia", op_chains, 8u);
  assert(n_op >= 1u);
  assert(op_chains[0].operad_composition_score > 0.0);

  assert(http_handle_request(&ctx, "GET", "/api/v1/temporal", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"persistent_homology\":") != NULL);
  assert(strstr(json.data, "\"adjunction_monad\":") != NULL);
  assert(strstr(json.data, "\"temporal_sheaf\":") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/operad?r1=east-asia&r2=europe&r3=east-asia", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"operad_chains\":") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/feed.xml?category=all-news", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "<feed xmlns=\"http://www.w3.org/2005/Atom\">") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/stream", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "event: topology") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/console", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "TOPOS &amp; SHEAF INTELLIGENCE CONSOLE") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/pushout?left_region=east-asia&right_region=europe&over=mentioned_country", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"natural_transformation\":") != NULL);
  assert(strstr(json.data, "\"amalgams\":") != NULL);
  assert(strstr(json.data, "\"shared_witness\":\"TW\"") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/concepts?min_support=1", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"lawvere_tierney_closure\":") != NULL);
  assert(strstr(json.data, "\"subconcept_ids\":") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "GET", "/api/v1/security", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"clearance_levels\":") != NULL);
  buffer_free(&json);

  /* Ingest an AI neocloud / inference article and test /api/v1/ai and ?ai_model= / ?ai_infra= / ?hf_repo= / ?ai_license= fibers */
  uint32_t a4 = 0;
  engine_ingest_article(&eng,
                        f_tw,
                        "tw-ai-4",
                        "CoreWeave and Together AI scale deepseek-ai/DeepSeek-R1-Distill-Qwen-32B inference on NVIDIA Blackwell GB200 with vLLM",
                        "https://focustaiwan.tw/4",
                        "AI Desk",
                        "PagedAttention KV-cache and SGLang speculative decoding boost open-weights token throughput.",
                        "<p>CoreWeave GB200 NVL72 neocloud clusters serve deepseek-ai/DeepSeek-R1-Distill-Qwen-32B and meta-llama/Llama-3.3-70B-Instruct using vLLM FP8.</p>",
                        "2026-11-03T03:00:00Z",
                        1762130500000ULL,
                        "",
                        NULL,
                        0,
                        &a4,
                        &ins);
  assert(ins == 1);

  ArticleQuery ai_q;
  memset(&ai_q, 0, sizeof(ai_q));
  ai_q.ai_model = "DeepSeek-R1/V3";
  ai_q.ai_infra = "CoreWeave";
  ai_q.ai_tier = "neocloud-and-cluster";
  ai_q.ai_license = "open-weights-permissive";
  ai_q.hf_repo = "deepseek-ai/DeepSeek-R1-Distill-Qwen-32B";
  ai_q.limit = 10u;
  assert(engine_search(&eng, &ai_q, &res) == 0);
  assert(res.total == 1u);
  assert(res.hits[0].article->id == a4);
  search_response_free(&res);

  assert(http_handle_request(&ctx, "GET", "/api/v1/ai", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"ai_compute_tiers\":") != NULL);
  assert(strstr(json.data, "\"ai_license_spectrum\":") != NULL);
  assert(strstr(json.data, "\"open_vs_closed_summary\":") != NULL);
  assert(strstr(json.data, "\"DEEPSEEK-R1/V3\":1") != NULL);
  assert(strstr(json.data, "\"COREWEAVE\":1") != NULL);
  assert(strstr(json.data, "\"DEEPSEEK-AI/DEEPSEEK-R1-DISTILL-QWEN-32B\":1") != NULL);
  buffer_free(&json);

  /* Ingest a Sports / Champions League / Formula 1 article and test ?sport= and /api/v1/sports */
  uint32_t a5 = 0;
  engine_ingest_article(&eng,
                        f_eu,
                        "eu-sp-5",
                        "Real Madrid faces Bayern Munich in UEFA Champions League knockout as Formula 1 Grand Prix arrives in Monaco",
                        "https://euobserver.com/5",
                        "Sports Desk",
                        "Premier League and LaLiga clubs prepare for Champions League final alongside Scuderia Ferrari F1 upgrades.",
                        "<p>UEFA Champions League and Formula 1 headline European sports calendar.</p>",
                        "2026-11-03T04:00:00Z",
                        1762130900000ULL,
                        "",
                        NULL,
                        0,
                        &a5,
                        &ins);
  assert(ins == 1);

  ArticleQuery sp_q;
  memset(&sp_q, 0, sizeof(sp_q));
  sp_q.sport = "UEFA-Champions-League";
  sp_q.limit = 10u;
  assert(engine_search(&eng, &sp_q, &res) == 0);
  assert(res.total == 1u);
  assert(res.hits[0].article->id == a5);
  search_response_free(&res);

  assert(http_handle_request(&ctx, "GET", "/api/v1/sports", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"sports_articles_count\":1") != NULL);
  assert(strstr(json.data, "\"UEFA-CHAMPIONS-LEAGUE\":1") != NULL);
  assert(strstr(json.data, "\"FORMULA-1\":1") != NULL);
  buffer_free(&json);

  m1_tokenizer_free(&tok);
  engine_free(&loaded);
  engine_free(&eng);
}

typedef struct {
  const OyezEngine *eng;
  size_t iterations;
  size_t hits_sum;
} ConcurrentSearchWorkerArg;

static void *concurrent_search_worker(void *arg) {
  ConcurrentSearchWorkerArg *w = (ConcurrentSearchWorkerArg *)arg;
  size_t acc = 0;
  for(size_t i = 0; i < w->iterations; ++i) {
    engine_read_lock(w->eng);
    ArticleQuery q;
    memset(&q, 0, sizeof(q));
    q.query = (i & 1u) ? "semiconductor wafer" : "DeepSeek CoreWeave";
    q.category = "all-news";
    q.use_category_morphism = 1;
    q.limit = 10u;
    q.offset = i % 3u;
    SearchResponse res;
    if(engine_search(w->eng, &q, &res) == 0) {
      acc += res.total;
      search_response_free(&res);
    }
    engine_unlock(w->eng);
  }
  w->hits_sum = acc;
  return NULL;
}

static void test_hardening_and_concurrency(void) {
  /* 1. Overflow-safe allocate & XML escaping */
  assert(allocate(SIZE_MAX, 2u) == NULL);

  Buffer xbuf;
  buffer_init(&xbuf);
  xml_cstr(&xbuf, "a&b<c>\"d\"'e'\x01]]>");
  assert(strcmp(xbuf.data, "a&amp;b&lt;c&gt;&quot;d&quot;&apos;e&apos;]]&gt;") == 0);
  buffer_free(&xbuf);

  /* 2. Malformed & surrogate HTML numeric entities */
  Buffer ebuf;
  buffer_init(&ebuf);
  const char *bad_ent = "ok&#xD800;&#55296;&#x110000;&#99999999999999;&amp;";
  assert(markup_decode_entities(bad_ent, strlen(bad_ent), &ebuf) == 0);
  assert(strstr(ebuf.data, "ok") != NULL);
  assert(strstr(ebuf.data, "&") != NULL);
  buffer_free(&ebuf);

  /* 3. Out-of-bounds RFC3339 & RFC2822 timestamps */
  assert(parse_timestamp_ms("2026-11-03T25:00:00Z") == 0u);
  assert(parse_timestamp_ms("2026-11-03T12:65:00Z") == 0u);
  assert(parse_timestamp_ms("999999999999 Nov 2026 12:00:00 GMT") == 0u);
  assert(parse_timestamp_ms("03 Nov 999999999999 12:00:00 GMT") == 0u);

  /* 4. Adversarial XML & JSON escaping + 8-thread concurrent query_cache stress test */
  OyezEngine eng;
  engine_init(&eng);
  uint32_t fid = 0;
  CatalogEntry ce = {"Sec Feed", "https://sec.example/rss", "https://sec.example",
                     "cyber", REGION_NORTH_AMERICA, "US", "North America", "en", "specialist", "Sec"};
  engine_upsert_feed(&eng, &ce, &fid);

  int ins = 0;
  engine_ingest_article(&eng,
                        fid,
                        "adv-1",
                        "Semiconductor wafer ]]> <script>&\"' alert</script>",
                        "https://sec.example/a?x=1&y=2\"evil",
                        "Sec",
                        "DeepSeek CoreWeave ]]> summary & <tag>",
                        "<p>Semiconductor wafer DeepSeek CoreWeave analysis.</p>",
                        "2026-11-03T05:00:00Z",
                        1762135000000ULL,
                        "",
                        NULL,
                        0,
                        NULL,
                        &ins);
  assert(ins == 1);

  ServerContext ctx = {&eng, NULL, NULL, 0};
  int status = 0;
  Buffer out;
  assert(http_handle_request(&ctx, "GET", "/api/v1/feed.xml", "", 0, &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "]]&gt; &lt;script&gt;&amp;&quot;&apos;") != NULL);
  assert(strstr(out.data, "href=\"https://sec.example/a?x=1&amp;y=2&quot;evil\"") != NULL);
  buffer_free(&out);

  assert(http_handle_request(&ctx, "GET", "/api/v1/ontology?sub=ai%22evil&sup=cyber", "", 0, &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "\"sub\":\"ai\\\"evil\"") != NULL);
  buffer_free(&out);

  pthread_t threads[8];
  ConcurrentSearchWorkerArg args[8];
  for(int t = 0; t < 8; ++t) {
    args[t].eng = &eng;
    args[t].iterations = 200u;
    args[t].hits_sum = 0;
    assert(pthread_create(&threads[t], NULL, concurrent_search_worker, &args[t]) == 0);
  }
  for(int t = 0; t < 8; ++t) {
    pthread_join(threads[t], NULL);
    assert(args[t].hits_sum == 200u);
  }

  engine_free(&eng);
}

int main(void) {
  test_checksum_and_wire();
  test_bpe_and_m1_tokenizer();
  test_markup_and_readability();
  test_category_theory_and_ontology();
  test_engine_search_and_categorical_endpoints();
  test_hardening_and_concurrency();
  printf("ALL C23 OYEZ TESTS PASSED\n");
  return 0;
}
