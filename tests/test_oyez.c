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
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int mask_subset32(uint32_t a, uint32_t b) {
  return (a & ~b) == 0u;
}

static uint32_t mask_popcount32(uint32_t mask) {
  uint32_t count = 0u;
  while(mask) {
    mask &= mask - 1u;
    count++;
  }
  return count;
}

static uint32_t mask_ctz32(uint32_t mask) {
  uint32_t index = 0u;
  assert(mask != 0u);
  while((mask & 1u) == 0u) {
    mask >>= 1u;
    index++;
  }
  return index;
}

static double abs_diff(double a, double b) {
  double d = a - b;
  return d < 0.0 ? -d : d;
}

static void test_checksum_and_wire(void) {
  const char *msg = "oyez-c99-wire-test";
  uint32_t crc = checksum_crc32(msg, strlen(msg));
  assert(crc != 0u);

  /* Pure first-party C99 64-bit bit-manipulation and 128-bit limb multiplication checks */
  assert(popcnt64(0ULL) == 0u);
  assert(popcnt64(UINT64_MAX) == 64u);
  assert(popcnt64(0x5555555555555555ULL) == 32u);
  assert(popcnt64(0xAAAAAAAAAAAAAAAAULL) == 32u);
  assert(ctz64(0ULL) == 64u);
  assert(clz64(0ULL) == 64u);
  for(unsigned b = 0; b < 64u; ++b) {
    uint64_t single = 1ULL << b;
    assert(popcnt64(single) == 1u);
    assert(ctz64(single) == b);
    assert(clz64(single) == 63u - b);
    assert(rotl64(single, 0u) == single);
    assert(rotl64(single, 64u) == single);
  }
  uint64_t lo = 0, hi = 0;
  mul64x64_128(0ULL, UINT64_MAX, &lo, &hi);
  assert(lo == 0ULL && hi == 0ULL);
  mul64x64_128(1ULL, UINT64_MAX, &lo, &hi);
  assert(lo == UINT64_MAX && hi == 0ULL);
  mul64x64_128(UINT64_MAX, UINT64_MAX, &lo, &hi);
  assert(lo == 1ULL && hi == UINT64_MAX - 1ULL);
  assert(wyhash64(0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL) != 0ULL);

  /* Pure first-party C99 IEEE-754 binary64 & transcendental math checks */
  assert(f64_is_inf(f64_inf()) == 1);
  assert(f64_is_inf(f64_neg_inf()) == 1);
  assert(f64_is_finite(f64_inf()) == 0);
  assert(f64_is_finite(f64_neg_inf()) == 0);
  assert(f64_is_finite(0.0) == 1);
  assert(f64_is_nan(f64_from_bits(0x7FF8000000000001ULL)) == 1);
  assert(f64_is_finite(f64_from_bits(0x7FF8000000000001ULL)) == 0);
  assert(f64_abs(-42.5) == 42.5);
  assert(f64_abs(42.5) == 42.5);
  assert(abs_diff(f64_sqrt(2.0), 1.4142135623730950488) < 1e-14);
  assert(abs_diff(f64_sqrt(9.0), 3.0) < 1e-15);
  assert(abs_diff(f64_ln(1.0), 0.0) < 1e-15);
  assert(abs_diff(f64_ln(2.71828182845904523536), 1.0) < 1e-13);
  assert(abs_diff(f64_exp(0.0), 1.0) < 1e-15);
  assert(abs_diff(f64_exp(1.0), 2.71828182845904523536) < 1e-13);
  assert(abs_diff(f64_ln(f64_exp(3.25)), 3.25) < 1e-12);
  assert(abs_diff(f64_exp(f64_ln(17.5)), 17.5) < 1e-11);
  assert(abs_diff(f64_atan(0.0), 0.0) < 1e-15);
  assert(abs_diff(f64_atan(1.0), 0.78539816339744830962) < 1e-13);
  assert(abs_diff(f64_atan(2.5) + f64_atan(0.4), 1.57079632679489661923) < 1e-13);
  assert(abs_diff(f64_cos(0.0), 1.0) < 1e-15);
  assert(abs_diff(f64_cos(1.04719755119659774615), 0.5) < 1e-12);
  assert(abs_diff(f64_cos(3.14159265358979323846), -1.0) < 1e-12);
  assert(abs_diff(f64_cos(-0.75), f64_cos(0.75)) < 1e-15);

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
  /* Exhaustively verify that the ontology is a finite poset with a terminal object. */
  uint32_t counted_morphisms = 0u;
  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    assert(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)a) == 1);
    assert(ontology_has_morphism((OntologyNodeId)a, ONTO_ROOT) == 1);
    for(uint32_t b = 0; b < ONTOLOGY_NODE_COUNT; ++b) {
      if(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)b)) counted_morphisms++;
      if(a != b) {
        assert(!(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)b) &&
                 ontology_has_morphism((OntologyNodeId)b, (OntologyNodeId)a)));
      }
      for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
        if(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)b) &&
           ontology_has_morphism((OntologyNodeId)b, (OntologyNodeId)c)) {
          assert(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)c));
        }
      }
    }
  }

  /* The object poset is not a lattice.  Report all minimal upper and maximal
   * lower bounds instead of silently choosing one of several incomparable
   * candidates.  Validate the bound APIs and unique-bound adapters for every
   * unordered object pair. */
  uint32_t counted_unique_joins = 0u, counted_ambiguous_joins = 0u, counted_missing_joins = 0u;
  uint32_t counted_unique_meets = 0u, counted_ambiguous_meets = 0u, counted_missing_meets = 0u;
  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    for(uint32_t b = a; b < ONTOLOGY_NODE_COUNT; ++b) {
      uint32_t upper = ontology_minimal_upper_bounds((OntologyNodeId)a, (OntologyNodeId)b);
      uint32_t lower = ontology_maximal_lower_bounds((OntologyNodeId)a, (OntologyNodeId)b);
      uint32_t common_up = ontology_node((OntologyNodeId)a)->upward_mask &
                           ontology_node((OntologyNodeId)b)->upward_mask;
      uint32_t common_down = ontology_node((OntologyNodeId)a)->downward_mask &
                             ontology_node((OntologyNodeId)b)->downward_mask;
      assert(upper == ontology_minimal_upper_bounds((OntologyNodeId)b, (OntologyNodeId)a));
      assert(lower == ontology_maximal_lower_bounds((OntologyNodeId)b, (OntologyNodeId)a));
      assert(lower == ontology_meet_mask((OntologyNodeId)a, (OntologyNodeId)b));

      uint32_t scan = upper;
      while(scan) {
        uint32_t u = mask_ctz32(scan);
        scan &= scan - 1u;
        assert(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)u));
        assert(ontology_has_morphism((OntologyNodeId)b, (OntologyNodeId)u));
        uint32_t strict_common_below_u = common_up &
                                         ontology_node((OntologyNodeId)u)->downward_mask &
                                         ~(1u << u);
        assert(strict_common_below_u == 0u);
      }
      scan = lower;
      while(scan) {
        uint32_t u = mask_ctz32(scan);
        scan &= scan - 1u;
        assert(ontology_has_morphism((OntologyNodeId)u, (OntologyNodeId)a));
        assert(ontology_has_morphism((OntologyNodeId)u, (OntologyNodeId)b));
        uint32_t strict_common_above_u = common_down &
                                         ontology_node((OntologyNodeId)u)->upward_mask &
                                         ~(1u << u);
        assert(strict_common_above_u == 0u);
      }
      scan = common_up;
      while(scan) {
        uint32_t u = mask_ctz32(scan);
        scan &= scan - 1u;
        assert((ontology_node((OntologyNodeId)u)->downward_mask & upper) != 0u);
      }
      scan = common_down;
      while(scan) {
        uint32_t u = mask_ctz32(scan);
        scan &= scan - 1u;
        assert((ontology_node((OntologyNodeId)u)->upward_mask & lower) != 0u);
      }

      OntologyNodeId unique = ONTO_ROOT;
      uint32_t upper_count = mask_popcount32(upper);
      if(upper_count == 1u) {
        assert(ontology_unique_join((OntologyNodeId)a, (OntologyNodeId)b, &unique) == 0);
        assert((upper & (1u << unique)) != 0u);
        assert(ontology_join((OntologyNodeId)a, (OntologyNodeId)b) == unique);
        counted_unique_joins++;
      } else {
        assert(ontology_unique_join((OntologyNodeId)a, (OntologyNodeId)b, &unique) == -1);
        assert(ontology_join((OntologyNodeId)a, (OntologyNodeId)b) == ONTO_NONE);
        if(upper_count == 0u) counted_missing_joins++;
        else counted_ambiguous_joins++;
      }
      uint32_t lower_count = mask_popcount32(lower);
      if(lower_count == 1u) {
        assert(ontology_unique_meet((OntologyNodeId)a, (OntologyNodeId)b, &unique) == 0);
        assert((lower & (1u << unique)) != 0u);
        counted_unique_meets++;
      } else {
        assert(ontology_unique_meet((OntologyNodeId)a, (OntologyNodeId)b, &unique) == -1);
        if(lower_count == 0u) counted_missing_meets++;
        else counted_ambiguous_meets++;
      }
    }
  }
  assert(ontology_minimal_upper_bounds(ONTO_AI, ONTO_SPACE) ==
         ((1u << ONTO_SCIENCE) | (1u << ONTO_TECHNOLOGY)));
  OntologyNodeId ambiguous_join = ONTO_ROOT;
  assert(ontology_unique_join(ONTO_AI, ONTO_SPACE, &ambiguous_join) == -1);
  assert(strcmp(ontology_node(ontology_join(ONTO_AI, ONTO_SPACE))->slug, "none") == 0);

  OntologyOrderAudit order_audit;
  ontology_order_audit(&order_audit);
  assert(order_audit.object_count == ONTOLOGY_NODE_COUNT);
  assert(order_audit.morphism_count == counted_morphisms);
  assert(order_audit.pair_count == (ONTOLOGY_NODE_COUNT * (ONTOLOGY_NODE_COUNT + 1u)) / 2u);
  assert(order_audit.is_reflexive == 1);
  assert(order_audit.is_antisymmetric == 1);
  assert(order_audit.is_transitive == 1);
  assert(order_audit.is_lattice == 0);
  assert(order_audit.unique_join_pairs == counted_unique_joins);
  assert(order_audit.ambiguous_join_pairs == counted_ambiguous_joins);
  assert(order_audit.missing_join_pairs == counted_missing_joins);
  assert(order_audit.unique_meet_pairs == counted_unique_meets);
  assert(order_audit.ambiguous_meet_pairs == counted_ambiguous_meets);
  assert(order_audit.missing_meet_pairs == counted_missing_meets);
  assert(order_audit.morphism_count == 170u);
  assert(order_audit.unique_join_pairs == 495u);
  assert(order_audit.ambiguous_join_pairs == 33u);
  assert(order_audit.missing_join_pairs == 0u);
  assert(order_audit.unique_meet_pairs == 205u);
  assert(order_audit.ambiguous_meet_pairs == 13u);
  assert(order_audit.missing_meet_pairs == 310u);

  /* The ideal completion is a genuine bounded distributive lattice.  Exercise
   * its operations and the Heyting/co-Heyting adjunctions over every principal
   * ideal plus bottom and top. */
  uint32_t ideals[ONTOLOGY_NODE_COUNT + 2u];
  size_t ideal_count = 0u;
  ideals[ideal_count++] = 0u;
  ideals[ideal_count++] = 0xFFFFFFFFu;
  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    uint32_t ideal = ontology_downward_closure(1u << a);
    assert(ontology_is_ideal(ideal));
    ideals[ideal_count++] = ideal;
  }
  assert(!ontology_is_ideal(1u << ONTO_ROOT));
  for(size_t ai = 0; ai < ideal_count; ++ai) {
    uint32_t a = ideals[ai];
    assert(ontology_is_ideal(a));
    assert(ontology_ideal_join(a, 0u) == a);
    assert(ontology_ideal_join(a, 0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(ontology_ideal_meet(a, 0u) == 0u);
    assert(ontology_ideal_meet(a, 0xFFFFFFFFu) == a);
    assert(ontology_ideal_join(a, a) == a);
    assert(ontology_ideal_meet(a, a) == a);
    for(size_t bi = 0; bi < ideal_count; ++bi) {
      uint32_t b = ideals[bi];
      uint32_t join_ab = ontology_ideal_join(a, b);
      uint32_t meet_ab = ontology_ideal_meet(a, b);
      uint32_t implication_ab = ontology_heyting_implication(a, b);
      uint32_t subtraction_ab = ontology_coheyting_subtraction(a, b);
      assert(ontology_is_ideal(join_ab));
      assert(ontology_is_ideal(meet_ab));
      assert(ontology_is_ideal(implication_ab));
      assert(ontology_is_ideal(subtraction_ab));
      assert(join_ab == ontology_ideal_join(b, a));
      assert(meet_ab == ontology_ideal_meet(b, a));
      assert(ontology_ideal_meet(a, join_ab) == a);
      assert(ontology_ideal_join(a, meet_ab) == a);
      for(size_t ci = 0; ci < ideal_count; ++ci) {
        uint32_t c = ideals[ci];
        assert(ontology_ideal_join(ontology_ideal_join(a, b), c) ==
               ontology_ideal_join(a, ontology_ideal_join(b, c)));
        assert(ontology_ideal_meet(ontology_ideal_meet(a, b), c) ==
               ontology_ideal_meet(a, ontology_ideal_meet(b, c)));
        assert(ontology_ideal_meet(a, ontology_ideal_join(b, c)) ==
               ontology_ideal_join(ontology_ideal_meet(a, b), ontology_ideal_meet(a, c)));
        assert(ontology_ideal_join(a, ontology_ideal_meet(b, c)) ==
               ontology_ideal_meet(ontology_ideal_join(a, b), ontology_ideal_join(a, c)));
        assert(mask_subset32(ontology_ideal_meet(c, a), b) ==
               mask_subset32(c, implication_ab));
        assert(mask_subset32(subtraction_ab, c) ==
               mask_subset32(a, ontology_ideal_join(b, c)));
      }
    }
  }

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
  double root_min_input[ONTOLOGY_NODE_COUNT];
  double root_min_ran[ONTOLOGY_NODE_COUNT];
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) root_min_input[c] = 10.0 + (double)c;
  root_min_input[ONTO_ROOT] = -7.0;
  ontology_right_kan_extend(root_min_input, root_min_ran);
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) assert(root_min_ran[c] == -7.0);
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

  /* Directed Hausdorff-Lawvere quasimetric.  The empty conventions are
   * sup(empty) = 0 and inf(empty) = +infinity. */
  assert(ontology_lawvere_distance(0u, 0u) == 0.0);
  assert(ontology_lawvere_distance(0u, 1u << ONTO_AI) == 0.0);
  assert(f64_is_inf(ontology_lawvere_distance(1u << ONTO_AI, 0u)));
  double dl_up = ontology_lawvere_distance(1u << ONTO_AI, 1u << ONTO_TECHNOLOGY);
  double dl_down = ontology_lawvere_distance(1u << ONTO_TECHNOLOGY, 1u << ONTO_AI);
  assert(dl_up < dl_down);
  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    assert(ontology_lawvere_distance(1u << a, 1u << a) == 0.0);
    for(uint32_t b = 0; b < ONTOLOGY_NODE_COUNT; ++b) {
      double dl_ab = ontology_lawvere_distance(1u << a, 1u << b);
      assert(dl_ab >= 0.0);
      for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
        double dl_ac = ontology_lawvere_distance(1u << a, 1u << c);
        double dl_bc = ontology_lawvere_distance(1u << b, 1u << c);
        assert(dl_ac <= dl_ab + dl_bc + 1e-9);
      }
    }
  }

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
  assert(monad.beck_coequalizer_defect < 1e-12);
  assert(monad.isbell_adjunction_defect < 1e-12);
  assert(monad.kleisli_entropy > 0.5);
  assert(monad.eilenberg_moore_mask != 0u);
  assert(monad.codensity_monad[ONTO_AI] >= raw[ONTO_AI] - 1e-9);
  assert(monad.bousfield_localized[ONTO_TECHNOLOGY] > 0.0);

  /* 1-Simplicial Hodge Coexact/Harmonic Pythagorean Decomposition:
   * ||R||_2^2 == ||F_coexact||_2^2 + ||F_harm||_2^2 and Tr(L_1) == 2*f_1 + 3*f_2 */
  double sol_pythag_err = sheaf.hodge_solenoidal_norm - (sheaf.hodge_coexact_norm + sheaf.hodge_harmonic_norm);
  if(sol_pythag_err < 0.0) sol_pythag_err = -sol_pythag_err;
  assert(sol_pythag_err < 1e-9);
  assert(sheaf.laplacian_1_trace == 2.0 * (double)sheaf.nerve_f_vector[1] + 3.0 * (double)sheaf.nerve_f_vector[2]);
  assert(sheaf.isbell_codensity_defect < 1e-12);

  /* Bi-Heyting Topos Calculus:
   * 1. Co-Heyting subtraction (A \co B) <= C  <==>  v(A) <= v(B) | v(C)
   * 2. Co-Heyting supplement ~A = 1 \co A satisfies Excluded Middle: v(A) | ~A == 0xFFFFFFFFu
   * 3. Lawvere Boundary d(A) = v(A) & ~A satisfies exact Leibniz derivation rule on downward-closed ideals:
   *      d(I & J) == (d(I) & J) | (I & d(J))
   * 4. Universal Quantifier ForAll(S) satisfies Galois adjunction: v(A) <= S  <==>  A <= ForAll(S)
   * 5. The exact subobject classifier is a stage-indexed natural family of sieves. */
  uint32_t ideal_tech = ontology_downward_closure(1u << ONTO_TECHNOLOGY);
  uint32_t ideal_sec = ontology_downward_closure(1u << ONTO_SECURITY);
  uint32_t co_sub_ts = ontology_coheyting_subtraction(ideal_tech, ideal_sec);
  /* Left-adjointness of co-Heyting subtraction: ideal_tech <= (ideal_sec | co_sub_ts) */
  assert((ideal_tech & ~(ideal_sec | co_sub_ts)) == 0u);
  /* Excluded middle of co-Heyting negation ~I: I | ~I == 0xFFFFFFFFu */
  uint32_t co_neg_tech = ontology_coheyting_negation(ideal_tech);
  assert((ideal_tech | co_neg_tech) == 0xFFFFFFFFu);
  /* Lawvere Boundary Leibniz Rule: d(I & J) == (d(I) & J) | (I & d(J)) */
  uint32_t bd_tech = ontology_coheyting_boundary(ideal_tech);
  uint32_t bd_sec = ontology_coheyting_boundary(ideal_sec);
  uint32_t bd_meet = ontology_coheyting_boundary(ideal_tech & ideal_sec);
  uint32_t leibniz_rhs = (bd_tech & ideal_sec) | (ideal_tech & bd_sec);
  assert(bd_meet == leibniz_rhs);
  /* Universal Quantifier right-adjointness */
  uint32_t s_arbitrary_mask = ideal_tech | (1u << ONTO_ROOT);
  uint32_t forall_s = ontology_universal_quantifier(s_arbitrary_mask);
  assert(forall_s == ideal_tech);
  /* For every representative ideal A, chi_A(c) is A intersected with the
   * principal ideal below c.  Restricting along d -> c gives chi_A(d). */
  for(size_t ai = 0; ai < ideal_count; ++ai) {
    uint32_t ideal = ideals[ai];
    double classifier_score[ONTOLOGY_NODE_COUNT];
    double compatibility_score[ONTOLOGY_NODE_COUNT];
    ontology_subobject_classifier_score(ideal, classifier_score);
    ontology_subobject_classifier_omega(ideal, compatibility_score);
    for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
      uint32_t sieve = ontology_subobject_classifier_sieve(ideal, (OntologyNodeId)c);
      assert(sieve == (ideal & ontology_node((OntologyNodeId)c)->downward_mask));
      assert(ontology_is_ideal(sieve));
      assert(classifier_score[c] >= 0.0 && classifier_score[c] <= 1.0);
      assert((classifier_score[c] == 1.0) == ((ideal & (1u << c)) != 0u));
      assert(abs_diff(classifier_score[c], compatibility_score[c]) < 1e-12);
      for(uint32_t d = 0; d < ONTOLOGY_NODE_COUNT; ++d) {
        if(ontology_has_morphism((OntologyNodeId)d, (OntologyNodeId)c)) {
          uint32_t restricted = sieve & ontology_node((OntologyNodeId)d)->downward_mask;
          assert(restricted == ontology_subobject_classifier_sieve(ideal, (OntologyNodeId)d));
        }
      }
    }
  }

  /* Enriched Isbell Conjugacy (O^* -| Spec_#):
   * 1. Unit F <= (F^*)^# and Counit (G^#)^* <= G
   * 2. Exact Triple Conjugation Identity ((F^*)^#)^* == F^*
   * 3. Representable Presheaf Y_c(u) = K - d_L(c, u) is an exact Isbell fixed point (Y_c^*)^# == Y_c */
  double f_star[ONTOLOGY_NODE_COUNT];
  double f_star_sharp[ONTOLOGY_NODE_COUNT];
  ontology_isbell_conjugate_covariant(raw, f_star);
  ontology_isbell_conjugate_contravariant(f_star, f_star_sharp);
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    assert(raw[u] <= f_star_sharp[u] + 1e-9);
  }
  assert(ontology_isbell_envelope_defect(raw) < 1e-12);
  double rep_ai[ONTOLOGY_NODE_COUNT];
  double rep_star[ONTOLOGY_NODE_COUNT];
  double rep_hull[ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    rep_ai[u] = 10.0 - ontology_lawvere_distance(1u << ONTO_AI, 1u << u);
  }
  ontology_isbell_conjugate_covariant(rep_ai, rep_star);
  ontology_isbell_conjugate_contravariant(rep_star, rep_hull);
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double rdiff = rep_hull[u] > rep_ai[u] ? (rep_hull[u] - rep_ai[u]) : (rep_ai[u] - rep_hull[u]);
    assert(rdiff < 1e-9);
  }

  /* Enriched Profunctor End & Weighted Colimit-Limit Tensor-Hom Adjunction:
   *   K - colim_W^L(F) == lim_L^W(K - F) */
  double end_val = ontology_profunctor_end(kan, kan);
  assert(end_val > 0.0);
  double w_colim = ontology_weighted_colimit(raw, kan);
  double w_lim = ontology_weighted_limit(raw, kan);
  assert(w_colim >= w_lim);
  double dual_kan[ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) dual_kan[u] = 100.0 - kan[u];
  double w_lim_dual = ontology_weighted_limit(raw, dual_kan);
  double th_err = (100.0 - w_colim) - w_lim_dual;
  if(th_err < 0.0) th_err = -th_err;
  assert(th_err < 1e-12);

  /* Symmetric cocone convolution and its max-plus residual.  It is associative
   * and commutative; unrestricted arrays have no global unit, while constant
   * zero is a unit on isotone sections (not representable by an object here). */
  double cocone_fg[ONTOLOGY_NODE_COUNT], cocone_gf[ONTOLOGY_NODE_COUNT];
  double cocone_gh[ONTOLOGY_NODE_COUNT], cocone_left[ONTOLOGY_NODE_COUNT];
  double cocone_right[ONTOLOGY_NODE_COUNT], cocone_hom_gh[ONTOLOGY_NODE_COUNT];
  double zero_section[ONTOLOGY_NODE_COUNT] = {0};
  double isotone_unit_check[ONTOLOGY_NODE_COUNT];
  double unrestricted_unit_check[ONTOLOGY_NODE_COUNT];
  ontology_cocone_convolution(raw, kan, cocone_fg);
  ontology_cocone_convolution(kan, raw, cocone_gf);
  ontology_cocone_convolution(kan, ran, cocone_gh);
  ontology_cocone_convolution(cocone_fg, ran, cocone_left);
  ontology_cocone_convolution(raw, cocone_gh, cocone_right);
  ontology_cocone_internal_hom(kan, ran, cocone_hom_gh);
  ontology_cocone_convolution(kan, zero_section, isotone_unit_check);
  ontology_cocone_convolution(raw, zero_section, unrestricted_unit_check);
  double cocone_in_place[ONTOLOGY_NODE_COUNT];
  double hom_in_place[ONTOLOGY_NODE_COUNT];
  memcpy(cocone_in_place, raw, sizeof(cocone_in_place));
  memcpy(hom_in_place, ran, sizeof(hom_in_place));
  ontology_cocone_convolution(cocone_in_place, kan, cocone_in_place);
  ontology_cocone_internal_hom(kan, hom_in_place, hom_in_place);
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    assert(abs_diff(cocone_fg[c], cocone_gf[c]) < 1e-12);
    assert(abs_diff(cocone_left[c], cocone_right[c]) < 1e-12);
    assert(f64_is_finite(cocone_hom_gh[c]));
    assert(abs_diff(cocone_in_place[c], cocone_fg[c]) < 1e-12);
    assert(abs_diff(hom_in_place[c], cocone_hom_gh[c]) < 1e-12);
    assert(abs_diff(isotone_unit_check[c], kan[c]) < 1e-12);
  }
  assert(unrestricted_unit_check[ONTO_TECHNOLOGY] > raw[ONTO_TECHNOLOGY]);
  assert(cocone_fg[ONTO_ROOT] >= raw[ONTO_AI] + kan[ONTO_CYBER]);
  assert(ontology_cocone_adjunction_defect(raw, kan, ran) < 1e-12);

  /* The historical Day-named entry points remain exact compatibility aliases. */
  double compatibility_fg[ONTOLOGY_NODE_COUNT], compatibility_hom[ONTOLOGY_NODE_COUNT];
  ontology_day_convolution(raw, kan, compatibility_fg);
  ontology_day_internal_hom(kan, ran, compatibility_hom);
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    assert(abs_diff(compatibility_fg[c], cocone_fg[c]) < 1e-12);
    assert(abs_diff(compatibility_hom[c], cocone_hom_gh[c]) < 1e-12);
  }
  assert(abs_diff(ontology_day_adjunction_defect(raw, kan, ran),
                  ontology_cocone_adjunction_defect(raw, kan, ran)) < 1e-12);

  /* Entropic Sinkhorn-Knopp Optimal Transport & Debiased Sinkhorn Divergence:
   *   1. S_eps(p, p) == 0.0 (exact self-debiased zero)
   *   2. S_eps(p, q) > 0.0 for distinct densities p != q
   *   3. Marginal scaling defect ||Pi*1 - p||_1 + ||Pi^T*1 - q||_1 < 1e-6 */
  SinkhornTransportState ot_self, ot_pq;
  ontology_sinkhorn_optimal_transport(raw, raw, 0.75, &ot_self);
  assert(ot_self.sinkhorn_divergence < 1e-12);
  assert(ot_self.fisher_rao_geodesic < 1e-12);
  assert(ot_self.marginal_defect_l1 < 1e-6);
  ontology_sinkhorn_optimal_transport(raw, kan, 0.75, &ot_pq);
  assert(ot_pq.entropic_wasserstein > 0.0);
  assert(ot_pq.sinkhorn_divergence > 0.0);
  assert(ot_pq.coupling_entropy > 0.0);
  assert(ot_pq.marginal_defect_l1 < 1e-6);
  assert(ot_pq.cheeger_constant > 0.0);
  assert(ot_pq.cheeger_buser_holds == 1);

  /* Riemannian Fisher-Rao Geodesic Metric on Statistical Simplex Delta^31(C):
   *   1. d_FR(p, p) == 0
   *   2. Symmetry d_FR(p, q) == d_FR(q, p)
   *   3. Triangle inequality d_FR(p, r) <= d_FR(p, q) + d_FR(q, r) */
  double dfr_pp = ontology_fisher_rao_distance(raw, raw);
  double dfr_pq = ontology_fisher_rao_distance(raw, kan);
  double dfr_qp = ontology_fisher_rao_distance(kan, raw);
  double dfr_qr = ontology_fisher_rao_distance(kan, ran);
  double dfr_pr = ontology_fisher_rao_distance(raw, ran);
  assert(dfr_pp < 1e-12);
  assert(dfr_pq > 0.0);
  double dfr_sym = dfr_pq > dfr_qp ? (dfr_pq - dfr_qp) : (dfr_qp - dfr_pq);
  assert(dfr_sym < 1e-12);
  assert(dfr_pr <= dfr_pq + dfr_qr + 1e-5);

  /* Spectral Heat Kernel Semigroup H_t = exp(-t L_0):
   *   1. Exact total mass conservation <1, H_t f> == <1, f>
   *   2. Constant sheaf 1 is an exact harmonic fixed point H_t(1) == 1 */
  double diffused[ONTOLOGY_NODE_COUNT];
  ontology_heat_kernel_propagate(raw, 0.35, diffused);
  double mass_before = 0.0, mass_after = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    mass_before += raw[u];
    mass_after += diffused[u];
  }
  double mass_err = mass_before > mass_after ? (mass_before - mass_after) : (mass_after - mass_before);
  assert(mass_err < 1e-9);
  double const_sheaf[ONTOLOGY_NODE_COUNT];
  double const_diff[ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) const_sheaf[u] = 5.0;
  ontology_heat_kernel_propagate(const_sheaf, 0.5, const_diff);
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double c_err = const_diff[u] > 5.0 ? (const_diff[u] - 5.0) : (5.0 - const_diff[u]);
    assert(c_err < 1e-12);
  }

  /* Rank-filtered page diagnostic.  The historical spectral-sequence entry
   * point remains an exact source alias, without claiming derived pages. */
  RankPageDiagnostic spec_seq;
  SpectralSequenceState legacy_spec_seq;
  ontology_rank_page_diagnostic(raw, &spec_seq);
  ontology_spectral_sequence(raw, &legacy_spec_seq);
  assert(spec_seq.euler_e0 == 1);
  assert(spec_seq.euler_e1 == 1);
  assert(spec_seq.euler_e2 == 1);
  assert(spec_seq.euler_e_inf == 1);
  assert(spec_seq.differential_nilpotency_defect == 0.0);
  assert(spec_seq.derived_pages_computed == 0);
  assert(spec_seq.e1_stratum_energy[0] > 0.0);
  assert(legacy_spec_seq.e1_stratum_energy[0] == spec_seq.e1_stratum_energy[0]);

  /* Modal closure diagnostic: j(M) == j(v(A)) and M is below the target. */
  uint32_t q_m = 0, q_fib = 0, legacy_q_m = 0, legacy_q_fib = 0;
  int q_weq = 0, legacy_q_weq = 0;
  ontology_modal_closure_diagnostic(
      1u << ONTO_AI, 1u << ONTO_SECURITY, &q_m, &q_fib, &q_weq);
  ontology_quillen_factorize(
      1u << ONTO_AI, 1u << ONTO_SECURITY, &legacy_q_m, &legacy_q_fib, &legacy_q_weq);
  assert(q_weq == 1);
  assert((q_m & ~q_fib) == 0u);
  assert(ontology_lawvere_tierney_sheafify(q_m) == ontology_lawvere_tierney_sheafify(1u << ONTO_AI));
  assert(legacy_q_m == q_m && legacy_q_fib == q_fib && legacy_q_weq == q_weq);

  /* Spivak Polynomial Endofunctor P_C(X) & Dependent Co-Design Lens */
  double poly_px[ONTOLOGY_NODE_COUNT], poly_lens[ONTOLOGY_NODE_COUNT];
  ontology_polynomial_functor_eval(raw, poly_px, poly_lens);
  assert(poly_px[ONTO_ROOT] > 0.0 && poly_px[ONTO_ROOT] <= 1.0);
  assert(poly_lens[ONTO_AI] > 0.0);

  /* Simplicial Gauss-Bonnet-Chern, Forman-Ricci, Ollivier-Ricci Curvature & Discrete Ricci Flow:
   *   1. Exact Simplicial Gauss-Bonnet-Chern sum_{u in C} K_GB(u) == chi(N(C)) == 1.0
   *   2. Normalized Hamilton-Ollivier Discrete Ricci Flow volume conservation */
  RicciCurvatureState ricci;
  ontology_ricci_curvature(raw, &ricci);
  assert(ricci.gauss_bonnet_defect < 1e-12);
  assert(ricci.ricci_flow_volume_defect < 1e-12);
  assert(ricci.ollivier_max_cluster >= ricci.ollivier_min_bridge);

  /* Ihara-Bass Graph Zeta Function, Bass Deformed Determinant & Tropical Convex Geometry:
   *   1. det(I - uA + u^2(D-I))|_{u=0} == 1.0 and det|_{u=1} == det(L_0) == 0.0
   *   2. Exact Tropical Convex Hull Idempotence ||pi_trop(pi_trop(F)) - pi_trop(F)||_2 == 0.0 */
  ZetaTropicalState zt;
  ontology_ihara_zeta_and_tropical(raw, 0.15, &zt);
  assert(zt.ihara_det_at_zero_defect < 1e-12);
  assert(zt.ihara_det_at_one_defect < 1e-11);
  assert(zt.ihara_bass_det > 0.0);
  assert(zt.ihara_zeta_val > 1.0);
  assert(zt.hashimoto_spectral_radius > 1.0);
  assert(zt.tropical_idempotence_defect < 1e-12);

  /* Commutative Rota-Baxter Algebra (A, R) of Weight theta = -1 & Atkinson Birkhoff Factorization:
   *   ||R(f) * R(g) - R(R(f)*g + f*R(g) - f*g)||_2 == 0.0 */
  RotaBaxterState rb;
  ontology_rota_baxter_algebra(raw, kan, &rb);
  assert(rb.rota_baxter_identity_defect < 1e-11);
  assert(rb.spitzer_birkhoff_defect < 1e-11);

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

  /* Atmospheric Physics (Lorenz-63 Chaos, Clausius-Clapeyron, Stull Wet-Bulb, Rossby Waves)
   * & Severe Weather Entity Extraction */
  WeatherPhysicsState wx_phys;
  ontology_atmospheric_dynamics(28.0, 75.0, 18.0, 38.0, 4.0, &wx_phys);
  assert(wx_phys.lyapunov_max > 0.5 && wx_phys.lyapunov_max < 1.8);
  assert(wx_phys.lyapunov_sum_div < -13.6 && wx_phys.lyapunov_sum_div > -13.7);
  assert(wx_phys.kaplan_yorke_dim > 2.02 && wx_phys.kaplan_yorke_dim < 2.15);
  assert(wx_phys.predictability_horizon > 0.5);
  assert(wx_phys.saturation_vapor_hpa > 37.0 && wx_phys.saturation_vapor_hpa < 38.5);
  assert(wx_phys.wet_bulb_celsius > 23.5 && wx_phys.wet_bulb_celsius < 25.5);
  assert(wx_phys.rossby_stationary_k > 3.0 && wx_phys.rossby_stationary_k < 8.0);

  ExtractedEntities wx_ent;
  entities_extract("NOAA National Hurricane Center issues Category 4 hurricane warning as rapid intensification and storm surge threaten coast",
                   "Storm Prediction Center tornado watch and geomagnetic storm X-class solar flare alert accompany USGS M7.1 earthquake.",
                   "ECMWF and GraphCast AI weather forecast track atmospheric river and polar vortex jet stream omega block.",
                   "weather",
                   NULL,
                   0,
                   &wx_ent);
  assert(wx_ent.weather_severity == WEATHER_CATASTROPHIC);
  assert(wx_ent.weather_score > 20.0);
  assert(wx_ent.weather_count >= 5u);
  assert((wx_ent.direct_ontology_mask & (1u << ONTO_CLIMATE)) != 0u);
  assert((wx_ent.closure_ontology_mask & (1u << ONTO_PLANETARY_SYSTEMS)) != 0u);
}

static void test_engine_search_and_categorical_endpoints(void) {
  OyezEngine eng;
  engine_init(&eng);

  size_t cat_total = 0;
  catalog_entries(&cat_total);
  assert(cat_total == 750u);
  size_t seeded = engine_seed_catalog(&eng, NULL, NULL);
  assert(seeded == 750u);

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

  /* A large offset must not alias the cached first page. */
#if SIZE_MAX > UINT32_MAX
  aq.limit = 1u;
  aq.offset = 0u;
  assert(engine_search(&eng, &aq, &res) == 0);
  assert(res.count == 1u);
  search_response_free(&res);
  aq.offset = (size_t)1u << 32u;
  assert(engine_search(&eng, &aq, &res) == 0);
  assert(res.total == 3u && res.count == 0u);
  search_response_free(&res);
#endif

  ArticleQuery language_q;
  memset(&language_q, 0, sizeof(language_q));
  language_q.language = "en";
  language_q.limit = 1u;
  assert(engine_search(&eng, &language_q, &res) == 0);
  assert(res.count == 1u);
  search_response_free(&res);
  language_q.language = "EN";
  assert(engine_search(&eng, &language_q, &res) == 0);
  assert(res.count == 0u);
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
  assert(http_handle_request(&ctx, "GET", "/api/v1/ontology?sub=ai&sup=knowledge-economy&a=ai&b=space", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"exists\":true") != NULL);
  assert(strstr(json.data, "\"euler_characteristic\":1") != NULL);
  assert(strstr(json.data, "\"fiedler_eigenvalue\":") != NULL);
  assert(strstr(json.data, "\"leinster_magnitude\":") != NULL);
  assert(strstr(json.data, "\"hodge_gradient_norm\":") != NULL);
  assert(strstr(json.data, "\"hodge_coexact_norm\":") != NULL);
  assert(strstr(json.data, "\"hodge_harmonic_norm\":") != NULL);
  assert(strstr(json.data, "\"laplacian_1_trace\":") != NULL);
  assert(strstr(json.data, "\"isbell_codensity_defect\":") != NULL);
  assert(strstr(json.data, "\"profunctor_end_self\":") != NULL);
  assert(strstr(json.data, "\"weighted_colimit_self\":") != NULL);
  assert(strstr(json.data, "\"cheeger_constant\":") != NULL);
  assert(strstr(json.data, "\"cheeger_buser_holds\":true") != NULL);
  assert(strstr(json.data, "\"cocone_adjunction_defect_self\":") != NULL);
  assert(strstr(json.data, "\"day_adjunction_defect_self\":") != NULL);
  assert(strstr(json.data, "\"order_audit\":{") != NULL);
  assert(strstr(json.data, "\"order\":{") != NULL);
  assert(strstr(json.data, "\"lattice\":{\"deprecated\":true") != NULL);
  assert(strstr(json.data, "\"minimal_upper_bounds\":") != NULL);
  assert(strstr(json.data, "\"maximal_lower_bounds\":") != NULL);
  assert(strstr(json.data, "\"unique_join\":") != NULL);
  assert(strstr(json.data, "\"unique_join\":null") != NULL);
  assert(strstr(json.data, "\"unique_meet\":") != NULL);
  assert(strstr(json.data, "\"ideal_join\":") != NULL);
  assert(strstr(json.data, "\"ideal_meet\":") != NULL);
  assert(strstr(json.data, "\"characteristic_sieve_a_at_b\":") != NULL);
  assert(strstr(json.data, "\"coheyting_subtraction\":") != NULL);
  assert(strstr(json.data, "\"lawvere_boundary_a\":") != NULL);
  assert(strstr(json.data, "\"yoneda_kan_score\":") != NULL);
  assert(strstr(json.data, "\"lawvere_distance_ab\":") != NULL);
  assert(strstr(json.data, "\"lawvere_tierney_sheaf_a\":") != NULL);
  buffer_free(&json);

  ToposState topos_st;
  engine_topos_state(&eng, REGION_EAST_ASIA, REGION_EUROPE, ONTO_AI, ONTO_TECHNOLOGY, &topos_st);
  assert(topos_st.leibniz_rule_holds == 1);
  assert(topos_st.isbell_envelope_defect < 1e-12);
  assert(topos_st.tensor_hom_adjunction_defect < 1e-12);
  assert(topos_st.cocone_adjunction_defect < 1e-12);
  assert(topos_st.day_adjunction_defect == topos_st.cocone_adjunction_defect);
  assert(topos_st.modal_closure_matches == 1);
  assert(topos_st.sinkhorn_ot.cheeger_buser_holds == 1);
  assert(topos_st.sinkhorn_ot.marginal_defect_l1 < 1e-6);
  assert(topos_st.rank_page_diagnostics.euler_e0 == 1 &&
         topos_st.rank_page_diagnostics.euler_e_inf == 1);
  assert(topos_st.ricci_curv.gauss_bonnet_defect < 1e-12);
  assert(topos_st.ricci_curv.ricci_flow_volume_defect < 1e-12);
  assert(topos_st.zeta_tropical.ihara_det_at_zero_defect < 1e-12);
  assert(topos_st.zeta_tropical.ihara_det_at_one_defect < 1e-11);
  assert(topos_st.zeta_tropical.tropical_idempotence_defect < 1e-12);
  assert(topos_st.rota_baxter.rota_baxter_identity_defect < 1e-11);
  assert(topos_st.rota_baxter.spitzer_birkhoff_defect < 1e-11);
  assert(topos_st.profunctor_end > 0.0);
  assert(topos_st.profunctor_coend > 0.0);
  assert(topos_st.classifier_score_a[ONTO_AI] == 1.0);
  assert(topos_st.omega_truth_a[ONTO_AI] == topos_st.classifier_score_a[ONTO_AI]);

  assert(http_handle_request(&ctx, "GET", "/api/v1/topos?source_region=east-asia&target_region=europe&a=ai&b=technology", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"leibniz_rule_holds\":true") != NULL);
  assert(strstr(json.data, "\"isbell_and_enriched_limits\":") != NULL);
  assert(strstr(json.data, "\"subobject_classifier\":{\"score\":") != NULL);
  assert(strstr(json.data, "\"subobject_classifier_omega\":") != NULL);
  assert(strstr(json.data, "\"characteristic_sieve_a_at_b\":") != NULL);
  assert(strstr(json.data, "\"equalizer_subobject\":") != NULL);
  assert(strstr(json.data, "\"cocone_convolution\":") != NULL);
  assert(strstr(json.data, "\"cocone_adjunction_defect\":") != NULL);
  assert(strstr(json.data, "\"day_convolution\":{\"deprecated\":true") != NULL);
  assert(strstr(json.data, "\"modal_closure_diagnostic\":{\"closure_matches\":true") != NULL);
  assert(strstr(json.data, "\"quillen_model_factorization\":{\"deprecated\":true") != NULL);
  assert(strstr(json.data, "\"sinkhorn_optimal_transport\":") != NULL);
  assert(strstr(json.data, "\"rank_page_diagnostic\":{\"nerve_euler_characteristic\":1") != NULL);
  assert(strstr(json.data, "\"rank_strata\":[") != NULL);
  assert(strstr(json.data, "\"simplex_counts_by_rank\":[") != NULL);
  assert(strstr(json.data, "\"derived_pages_computed\":false") != NULL);
  assert(strstr(json.data, "\"spectral_sequence\":{\"deprecated\":true") != NULL);
  assert(strstr(json.data, "\"diagnostic_only\":true") != NULL);
  assert(strstr(json.data, "\"polynomial_functor_codesign\":") != NULL);
  assert(strstr(json.data, "\"ricci_curvature\":") != NULL);
  assert(strstr(json.data, "\"ihara_zeta_and_tropical\":") != NULL);
  assert(strstr(json.data, "\"rota_baxter_algebra\":") != NULL);
  buffer_free(&json);

  /* Fast Greedy Determinantal Point Process (DPP) Cholesky MAP reranking (?diversify=dpp) */
  ArticleQuery dpp_q;
  memset(&dpp_q, 0, sizeof(dpp_q));
  dpp_q.query = "semiconductor";
  dpp_q.category = "knowledge-economy";
  dpp_q.use_category_morphism = 1;
  dpp_q.diversify_mmr = 2; /* Cholesky MAP DPP */
  dpp_q.mmr_lambda = 0.55;
  dpp_q.limit = 10u;
  assert(engine_search(&eng, &dpp_q, &res) == 0);
  assert(res.total == 3u && res.count == 3u);
  search_response_free(&res);
  assert(http_handle_request(&ctx, "GET", "/api/v1/articles?q=semiconductor&diversify=dpp&lambda=0.55", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"total\":3") != NULL);
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
  assert(strstr(json.data, "FINITE POSET &amp; IDEAL ANALYTICS") != NULL);
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

  /* Ingest a Severe Weather / Tropical Cyclone / Space Weather article and test ?weather=, ?weather_severity=,
   * /api/v1/weather, UCB1 Bandit Scheduling, and /api/v1/automation */
  uint32_t a6 = 0;
  engine_ingest_article(&eng,
                        f_tw,
                        "tw-wx-6",
                        "NOAA National Hurricane Center issues Category 4 super typhoon hurricane warning with storm surge and X-class solar flare",
                        "https://focustaiwan.tw/6",
                        "Weather Desk",
                        "Rapid intensification and tornado watch accompany geomagnetic storm and atmospheric river.",
                        "<p>NOAA NHC Category 4 hurricane warning, storm surge, and X-class solar flare geomagnetic storm.</p>",
                        "2026-11-03T05:00:00Z",
                        1762131500000ULL,
                        "",
                        NULL,
                        0,
                        &a6,
                        &ins);
  assert(ins == 1);

  ArticleQuery wx_q;
  memset(&wx_q, 0, sizeof(wx_q));
  wx_q.weather = "Tropical-Cyclone";
  wx_q.weather_severity = "catastrophic";
  wx_q.limit = 10u;
  assert(engine_search(&eng, &wx_q, &res) == 0);
  assert(res.total == 1u);
  assert(res.hits[0].article->id == a6);
  search_response_free(&res);

  assert(http_handle_request(&ctx, "GET", "/api/v1/weather?temp_c=29.0&humidity=78.0", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"weather_articles_count\":1") != NULL);
  assert(strstr(json.data, "\"catastrophic-emergency\":1") != NULL);
  assert(strstr(json.data, "\"TROPICAL-CYCLONE\":1") != NULL);
  assert(strstr(json.data, "\"atmospheric_physics\":{\"lorenz_63_attractor\":") != NULL);
  assert(strstr(json.data, "\"kaplan_yorke_dimension\":") != NULL);
  assert(strstr(json.data, "\"wet_bulb_temperature_c\":") != NULL);
  buffer_free(&json);

  /* Verify built-in automation rules triggered on the severe weather & AI articles */
  assert(eng.automation_rule_count >= 5u);
  assert(eng.automation_alert_count >= 2u);

  BanditFeedPriority bandit_prio[8];
  size_t b_sel = engine_bandit_select_feeds(&eng, bandit_prio, 8u);
  assert(b_sel == 8u);
  assert(bandit_prio[0].ucb1_score >= bandit_prio[1].ucb1_score);

  assert(http_handle_request(&ctx, "GET", "/api/v1/automation", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"rule_count\":5") != NULL);
  assert(strstr(json.data, "\"bandit_schedule\":[") != NULL);
  assert(strstr(json.data, "\"severe-weather-hazard\"") != NULL);
  buffer_free(&json);

  const char *new_rule_body =
      "{\"name\":\"Custom Taiwan Chip Watch\",\"category\":\"semiconductors\","
      "\"query\":\"TSMC\",\"action\":\"alert-ring\"}";
  assert(http_handle_request(&ctx, "POST", "/api/v1/automation/rules",
                             new_rule_body, strlen(new_rule_body), &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"rule_id\":6") != NULL);
  assert(strstr(json.data, "\"rule_count\":6") != NULL);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "POST", "/api/v1/automation/run",
                             "{\"auto_poll_interval_sec\":120}", 30u, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"auto_cycles_completed\":1") != NULL);
  assert(eng.auto_poll_interval_sec == 120u);
  buffer_free(&json);

  assert(http_handle_request(&ctx, "DELETE", "/api/v1/automation/rules?id=6", "", 0, &status, &json) == 0);
  assert(status == 200);
  assert(strstr(json.data, "\"status\":\"deleted\"") != NULL);
  assert(eng.automation_rule_count == 5u);
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

  /* 5. Nested HTML tags in readability_extract & unclosed script/comment stripping */
  Buffer sbuf;
  buffer_init(&sbuf);
  const char *unclosed_html = "<p>Visible text</p><script>evil_payload();";
  assert(markup_strip_html(unclosed_html, strlen(unclosed_html), &sbuf) == 0);
  assert(strstr(sbuf.data, "Visible text") != NULL);
  assert(strstr(sbuf.data, "evil_payload") == NULL);
  buffer_free(&sbuf);

  const char *nested_article_html =
    "<html><body><article><div class=\"callout\"><article>Inner note.</article></div>"
    "<p>Outer main paragraph after nested article tag must be preserved in full.</p>"
    "</article></body></html>";
  ExtractedContent rres;
  assert(readability_extract(nested_article_html, strlen(nested_article_html), "https://sec.example/nested", &rres) == 0);
  assert(strstr(rres.text_content, "Outer main paragraph after nested article tag") != NULL);
  readability_free(&rres);

  /* 6. CVE deduplication and word-boundary enforcement */
  engine_ingest_article(&eng,
                        fid,
                        "adv-cve",
                        "Critical CVE-2026-9999 and fake abcve-2026-8888",
                        "https://sec.example/cve",
                        "Sec",
                        "Patch CVE-2026-9999 immediately; CVE-2026-9999 is actively exploited.",
                        "<p>Details on CVE-2026-9999 and abcve-2026-8888.</p>",
                        "2026-11-03T06:00:00Z",
                        1762138000000ULL,
                        "",
                        NULL,
                        0,
                        NULL,
                        &ins);
  assert(ins == 1);
  assert(eng.article_count == 2u);
  const ArticleRecord *cve_rec = &eng.articles[1];
  uint32_t cve_9999_count = 0;
  for(size_t i = 0; i < cve_rec->mentioned_cve_count; ++i) {
    if(strcmp(cve_rec->mentioned_cves[i], "CVE-2026-9999") == 0) {
      cve_9999_count++;
    }
    assert(strcmp(cve_rec->mentioned_cves[i], "CVE-2026-8888") != 0);
  }
  assert(cve_9999_count == 1u);

  /* 7. Corrupted snapshot rejection without state corruption */
  FILE *bad_snap = fopen("/tmp/oyez_corrupt_test.snap", "wb");
  assert(bad_snap != NULL);
  fwrite("OYEZSNAP_CORRUPT_HEADER_DATA", 1, 28, bad_snap);
  fclose(bad_snap);
  OyezEngine snap_eng;
  engine_init(&snap_eng);
  assert(engine_load_snapshot(&snap_eng, "/tmp/oyez_corrupt_test.snap") == -1);
  assert(snap_eng.article_count == 0u);
  engine_free(&snap_eng);
  remove("/tmp/oyez_corrupt_test.snap");

  /* A checksum-valid but truncated record must leave an existing engine intact. */
  Buffer malformed_snap;
  buffer_init(&malformed_snap);
  buffer_append(&malformed_snap, "OYEZIX01", 8u);
  wire_append_u32(&malformed_snap, 1u);
  wire_append_u32(&malformed_snap, 0u);
  wire_append_u32(&malformed_snap, checksum_crc32(malformed_snap.data, malformed_snap.len));
  FILE *malformed_fp = fopen("/tmp/oyez_valid_crc_truncated.snap", "wb");
  assert(malformed_fp != NULL);
  assert(fwrite(malformed_snap.data, 1u, malformed_snap.len, malformed_fp) == malformed_snap.len);
  fclose(malformed_fp);
  buffer_free(&malformed_snap);
  assert(engine_load_snapshot(&eng, "/tmp/oyez_valid_crc_truncated.snap") == -1);
  assert(eng.feed_count == 1u && eng.article_count == 2u);
  assert(engine_get_article_by_id(&eng, 1u) != NULL);
  remove("/tmp/oyez_valid_crc_truncated.snap");

  OyezEngine replacement;
  engine_init(&replacement);
  CatalogEntry other_feed = {"Other", "https://other.example/rss", "https://other.example",
                             "science", REGION_EUROPE, "GB", "Europe", "en", "specialist", "Other"};
  assert(engine_upsert_feed(&replacement, &other_feed, NULL) == 0);
  assert(engine_save_snapshot(&eng, "/tmp/oyez_replace_test.snap") == 0);
  assert(engine_load_snapshot(&replacement, "/tmp/oyez_replace_test.snap") == 0);
  assert(replacement.feed_count == eng.feed_count && replacement.article_count == eng.article_count);
  assert(engine_find_feed_by_url(&replacement, "https://other.example/rss") == NULL);
  assert(engine_get_article_by_id(&replacement, 2u) != NULL);
  engine_free(&replacement);
  remove("/tmp/oyez_replace_test.snap");

  /* 8. Deeply repeated unclosed CDATA recursion guard & CDATA/comment closing-tag shielding */
  Buffer cdata_in, cdata_out;
  buffer_init(&cdata_in);
  buffer_init(&cdata_out);
  buffer_text(&cdata_in, "<p>Safe prefix</p>");
  for(int i = 0; i < 2000; ++i) {
    buffer_text(&cdata_in, "<![CDATA[unclosed ");
  }
  assert(markup_strip_html(cdata_in.data, cdata_in.len, &cdata_out) == 0);
  assert(strstr(cdata_out.data, "Safe prefix") != NULL);
  buffer_free(&cdata_in);
  buffer_free(&cdata_out);

  const char *rss_cdata_close =
    "<rss version=\"2.0\"><channel><title>CDATA Shield Feed</title>"
    "<item><title>Item One</title>"
    "<!-- </item> inside comment must not close item -->"
    "<description><![CDATA[Literal </item> and </description> inside CDATA block]]></description>"
    "<link>https://sec.example/item-1</link></item>"
    "</channel></rss>";
  ParsedFeed pfeed;
  assert(feed_parse(rss_cdata_close, strlen(rss_cdata_close), "https://sec.example/rss", &pfeed) == 0);
  assert(pfeed.item_count == 1u);
  assert(strcmp(pfeed.items[0].url, "https://sec.example/item-1") == 0);
  assert(strstr(pfeed.items[0].summary, "Literal") != NULL);
  feed_free(&pfeed);

  /* 9. Exact calendar day-of-month, leap-year & timezone bounds */
  assert(parse_timestamp_ms("2026-02-31T12:00:00Z") == 0u);
  assert(parse_timestamp_ms("2026-04-31T12:00:00Z") == 0u);
  assert(parse_timestamp_ms("2026-02-29T12:00:00Z") == 0u);
  assert(parse_timestamp_ms("2024-02-29T12:00:00Z") > 0u);
  assert(parse_timestamp_ms("2026-11-03T12:00:00+25:00") == 0u);
  assert(parse_timestamp_ms("2026-11-03T12:00:00+02:65") == 0u);

  /* 10. M1Tokenizer sha256 seal updated immediately upon m1_tokenizer_train */
  M1Tokenizer t_seal;
  m1_tokenizer_init(&t_seal);
  char empty_sha[65];
  memcpy(empty_sha, t_seal.sha256, 65u);
  const char *train_sample = "categorical presheaf cohomology kan extension topological sheaf ";
  assert(m1_tokenizer_train(&t_seal, (const uint8_t *)train_sample, strlen(train_sample), 320u) == 0);
  assert(sha256_valid_hex(t_seal.sha256));
  assert(strcmp(t_seal.sha256, empty_sha) != 0);
  m1_tokenizer_free(&t_seal);

  /* 11. PageRank damping clamp (negative / zero / NaN) & empty-engine /api/v1/centrality safety */
  RankedEntity pr_ents[8];
  ScoredArticle pr_arts[8];
  size_t pr_n1 = engine_bipartite_pagerank(&eng, 10u, -1.0, pr_ents, 8u, pr_arts, 8u);
  size_t pr_n2 = engine_bipartite_pagerank(&eng, 10u, 0.0 / 0.0, pr_ents, 8u, pr_arts, 8u);
  assert(pr_n1 > 0u && pr_n1 == pr_n2);
  assert(pr_ents[0].score > 0.0 && pr_arts[0].score > 0.0);

  OyezEngine empty_eng;
  engine_init(&empty_eng);
  ServerContext empty_ctx = {&empty_eng, NULL, NULL, 0};
  Buffer cent_out;
  assert(http_handle_request(&empty_ctx, "GET", "/api/v1/centrality", "", 0, &status, &cent_out) == 0);
  assert(status == 200);
  assert(strcmp(cent_out.data, "{\"entities\":[],\"top_articles\":[]}") == 0);
  buffer_free(&cent_out);
  engine_free(&empty_eng);

  /* 12. SSRF local/loopback/metadata target rejection in http_fetch_url */
  Buffer ssrf_buf;
  assert(http_fetch_url("http://localhost:8787/api/v1/health", &ssrf_buf) == -1);
  assert(http_fetch_url("http://127.0.0.1:8787/api/v1/health", &ssrf_buf) == -1);
  assert(http_fetch_url("http://169.254.169.254/latest/meta-data/", &ssrf_buf) == -1);
  assert(http_fetch_url("http://[::1]:8787/api/v1/health", &ssrf_buf) == -1);
  assert(http_fetch_url("file:///etc/passwd", &ssrf_buf) == -1);

  engine_free(&eng);
}

typedef struct {
  ServerContext *ctx;
  int fd;
} HttpSocketTask;

static void *serve_test_socket(void *arg) {
  HttpSocketTask *task = (HttpSocketTask *)arg;
  http_serve_client(task->ctx, task->fd);
  return NULL;
}

static void http_socket_exchange(ServerContext *ctx, const char *request, size_t split_at,
                                 Buffer *response) {
  int fd[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
  HttpSocketTask task = {ctx, fd[1]};
  pthread_t thread;
  assert(pthread_create(&thread, NULL, serve_test_socket, &task) == 0);
  size_t len = strlen(request);
  assert(split_at <= len);
  if(split_at == 0) split_at = len;
  assert(write(fd[0], request, split_at) == (ssize_t)split_at);
  if(split_at < len) {
    struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000};
    nanosleep(&pause, NULL);
    assert(write(fd[0], request + split_at, len - split_at) == (ssize_t)(len - split_at));
  }
  shutdown(fd[0], SHUT_WR);
  buffer_init(response);
  char chunk[2048];
  ssize_t n;
  while((n = read(fd[0], chunk, sizeof(chunk))) > 0) buffer_append(response, chunk, (size_t)n);
  close(fd[0]);
  pthread_join(thread, NULL);
}

static size_t count_substring(const char *text, const char *needle) {
  size_t count = 0;
  size_t n = strlen(needle);
  const char *p = text;
  while((p = strstr(p, needle)) != NULL) {
    count++;
    p += n;
  }
  return count;
}

static void test_http_connection_framing(void) {
  OyezEngine eng;
  engine_init(&eng);
  ServerContext ctx = {&eng, NULL, NULL, 0};
  Buffer response;

  const char *two_gets =
      "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
      "GET /api/v1/stats HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  http_socket_exchange(&ctx, two_gets, 0, &response);
  assert(count_substring(response.data, "HTTP/1.1 200 OK") == 2u);
  buffer_free(&response);

  const char *body = "{\"html\":\"<p>Framed body text</p>\"}";
  char post_then_get[512];
  int written = snprintf(post_then_get, sizeof(post_then_get),
      "POST /api/v1/extract HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\n\r\n%s"
      "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
      strlen(body), body);
  assert(written > 0 && (size_t)written < sizeof(post_then_get));
  const char *body_start = strstr(post_then_get, "\r\n\r\n");
  assert(body_start != NULL);
  size_t split_at = (size_t)(body_start + 4u - post_then_get) + 5u;
  http_socket_exchange(&ctx, post_then_get, split_at, &response);
  assert(count_substring(response.data, "HTTP/1.1 200 OK") == 2u);
  assert(strstr(response.data, "Framed body text") != NULL);
  buffer_free(&response);

  const char *duplicate_length =
      "POST /api/v1/extract HTTP/1.1\r\nContent-Length: 1\r\ncontent-length: 2\r\n\r\nx";
  http_socket_exchange(&ctx, duplicate_length, 0, &response);
  assert(count_substring(response.data, "HTTP/1.1 400 Bad Request") == 1u);
  buffer_free(&response);

  const char *misleading_header =
      "GET /health HTTP/1.1\r\nX-Note: Content-Length: 100\r\nConnection: close\r\n\r\n";
  http_socket_exchange(&ctx, misleading_header, 0, &response);
  assert(count_substring(response.data, "HTTP/1.1 200 OK") == 1u);
  buffer_free(&response);

  const char *bare_newline =
      "GET /health HTTP/1.1\r\nX-Note: value\ncontinued\r\n\r\n";
  http_socket_exchange(&ctx, bare_newline, 0, &response);
  assert(count_substring(response.data, "HTTP/1.1 400 Bad Request") == 1u);
  buffer_free(&response);

  const char *chunked =
      "POST /api/v1/extract HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
  http_socket_exchange(&ctx, chunked, 0, &response);
  assert(count_substring(response.data, "HTTP/1.1 400 Bad Request") == 1u);
  buffer_free(&response);

  const char *incomplete_body =
      "POST /api/v1/extract HTTP/1.1\r\nContent-Length: 20\r\n\r\nshort";
  http_socket_exchange(&ctx, incomplete_body, 0, &response);
  assert(response.len == 0u);
  buffer_free(&response);

  engine_free(&eng);
}

static long file_size_or_zero(const char *path) {
  FILE *fp = fopen(path, "rb");
  if(!fp) return 0;
  assert(fseek(fp, 0, SEEK_END) == 0);
  long size = ftell(fp);
  fclose(fp);
  return size;
}

static void test_url_fetch_redirect_policy(void) {
  assert(http_url_is_public("http://1.1.1.1/") == 1);
  assert(http_url_is_public("http://127.0.0.1/") == 0);
  assert(http_url_is_public("http://127.1/") == 0);
  assert(http_url_is_public("http://2130706433/") == 0);
  assert(http_url_is_public("http://user@127.0.0.1/") == 0);
  assert(http_url_is_public("http://169.254.169.254/") == 0);
  assert(http_url_is_public("http://[::ffff:127.0.0.1]/") == 0);
  assert(http_url_is_public("http://[2002:7f00:1::]/") == 0);
  assert(http_url_is_public("http://1.1.1.1:999999999999999999/") == 0);
  assert(http_url_is_public("file:///etc/passwd") == 0);

  char dir[] = "/tmp/oyez_curl_test_XXXXXX";
  assert(mkdtemp(dir) != NULL);
  char script_path[256], calls_path[256], path_value[4096];
  assert(snprintf(script_path, sizeof(script_path), "%s/curl", dir) < (int)sizeof(script_path));
  assert(snprintf(calls_path, sizeof(calls_path), "%s/calls", dir) < (int)sizeof(calls_path));
  FILE *script = fopen(script_path, "wb");
  assert(script != NULL);
  const char *script_text =
      "#!/bin/sh\n"
      "url=''\n"
      "for arg do url=$arg; done\n"
      "printf x >> \"$OYEZ_TEST_CALLS\"\n"
      "case \"$url\" in\n"
      "  http://1.1.1.1/start) printf moved; printf '301 http://8.8.8.8/final' >&2 ;;\n"
      "  http://8.8.8.8/final) printf 'public feed'; printf '200 ' >&2 ;;\n"
      "  http://1.1.1.1/private-start) printf moved; printf '302 http://127.0.0.1/secret' >&2 ;;\n"
      "  *) printf unexpected; printf '500 ' >&2 ;;\n"
      "esac\n";
  assert(fwrite(script_text, 1u, strlen(script_text), script) == strlen(script_text));
  fclose(script);
  assert(chmod(script_path, 0700) == 0);

  const char *current_path = getenv("PATH");
  char *saved_path = strdup(current_path ? current_path : "");
  assert(saved_path != NULL);
  assert(snprintf(path_value, sizeof(path_value), "%s:%s", dir, saved_path) < (int)sizeof(path_value));
  assert(setenv("PATH", path_value, 1) == 0);
  assert(setenv("OYEZ_TEST_CALLS", calls_path, 1) == 0);

  Buffer fetched;
  assert(http_fetch_url("http://1.1.1.1/start", &fetched) == 0);
  assert(strcmp(fetched.data, "public feed") == 0);
  buffer_free(&fetched);
  assert(file_size_or_zero(calls_path) == 2);

  assert(http_fetch_url("http://1.1.1.1/private-start", &fetched) == -1);
  assert(file_size_or_zero(calls_path) == 3);

  assert(setenv("PATH", saved_path, 1) == 0);
  unsetenv("OYEZ_TEST_CALLS");
  free(saved_path);
  remove(script_path);
  remove(calls_path);
  rmdir(dir);

  /* Pure first-party C99 RFC 3986 redirect resolution & HTTP/1.1 wire client tests */
  char resolved[512];
  assert(http_resolve_redirect_url("http://example.com:8080/feeds/dir/rss.xml",
                                   "/atom.xml", resolved, sizeof(resolved)) == 0);
  assert(strcmp(resolved, "http://example.com:8080/atom.xml") == 0);
  assert(http_resolve_redirect_url("http://example.com/feeds/dir/rss.xml?v=1",
                                   "sub/latest.xml", resolved, sizeof(resolved)) == 0);
  assert(strcmp(resolved, "http://example.com/feeds/dir/sub/latest.xml") == 0);
  assert(http_resolve_redirect_url("https://example.com/feeds/rss.xml",
                                   "//cdn.example.org/feed.json#frag", resolved, sizeof(resolved)) == 0);
  assert(strcmp(resolved, "https://cdn.example.org/feed.json") == 0);

  /* First-party C99 HTTP/1.1 client exchange with Transfer-Encoding: chunked */
  int cfd[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cfd) == 0);
  const char *mock_chunked_resp =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/rss+xml\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6;ext=1\r\n"
      "<rss><\r\n"
      "7\r\n"
      "/rss>\r\n\r\n"
      "0\r\n"
      "\r\n";
  assert(write(cfd[1], mock_chunked_resp, strlen(mock_chunked_resp)) == (ssize_t)strlen(mock_chunked_resp));
  assert(shutdown(cfd[1], SHUT_WR) == 0);
  int c_status = 0;
  char c_loc[256];
  Buffer c_body;
  buffer_init(&c_body);
  assert(http_client_exchange_fd(cfd[0], "example.com", 80u, 80, "/rss.xml",
                                 &c_status, c_loc, sizeof(c_loc), &c_body) == 0);
  char req_seen[512];
  ssize_t req_n = read(cfd[1], req_seen, sizeof(req_seen) - 1u);
  assert(req_n > 0);
  req_seen[req_n] = '\0';
  assert(strstr(req_seen, "GET /rss.xml HTTP/1.1\r\n") != NULL);
  assert(strstr(req_seen, "Host: example.com\r\n") != NULL);
  close(cfd[0]);
  close(cfd[1]);
  assert(c_status == 200);
  assert(strcmp(c_body.data, "<rss></rss>\r\n") == 0);
  buffer_free(&c_body);

  /* First-party C99 HTTP/1.1 client exchange with 301 Location & Content-Length */
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cfd) == 0);
  const char *mock_redir_resp =
      "HTTP/1.1 301 Moved Permanently\r\n"
      "Location: /new-feed.xml\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "moved";
  assert(write(cfd[1], mock_redir_resp, strlen(mock_redir_resp)) == (ssize_t)strlen(mock_redir_resp));
  assert(shutdown(cfd[1], SHUT_WR) == 0);
  buffer_init(&c_body);
  assert(http_client_exchange_fd(cfd[0], "example.com", 8080u, 80, "/old.xml",
                                 &c_status, c_loc, sizeof(c_loc), &c_body) == 0);
  req_n = read(cfd[1], req_seen, sizeof(req_seen) - 1u);
  assert(req_n > 0);
  req_seen[req_n] = '\0';
  assert(strstr(req_seen, "Host: example.com:8080\r\n") != NULL);
  close(cfd[0]);
  close(cfd[1]);
  assert(c_status == 301);
  assert(strcmp(c_loc, "/new-feed.xml") == 0);
  assert(strcmp(c_body.data, "moved") == 0);
  buffer_free(&c_body);

  /* RFC 3986 section 5.2.4 dot-segment normalization on relative Location headers */
  assert(http_resolve_redirect_url("https://example.com/news/2026/index.html",
                                   "../rss/./feed.xml?fmt=atom",
                                   resolved, sizeof(resolved)) == 0);
  assert(strcmp(resolved, "https://example.com/news/rss/feed.xml?fmt=atom") == 0);
  assert(http_resolve_redirect_url("https://example.com/a/b/c/feed.xml",
                                   "/x/y/../z/./atom.xml",
                                   resolved, sizeof(resolved)) == 0);
  assert(strcmp(resolved, "https://example.com/x/z/atom.xml") == 0);

  /* Streaming chunked response with trailers on an open socket (no peer EOF required) */
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cfd) == 0);
  const char *mock_trailer_resp =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "4;ext=1\r\n"
      "pure\r\n"
      "4\r\n"
      "-c99\r\n"
      "0\r\n"
      "X-Oyez-Trailer: verified\r\n"
      "\r\n";
  assert(write(cfd[1], mock_trailer_resp, strlen(mock_trailer_resp)) == (ssize_t)strlen(mock_trailer_resp));
  buffer_init(&c_body);
  assert(http_client_exchange_fd(cfd[0], "example.com", 80u, 80, "/stream.xml",
                                 &c_status, c_loc, sizeof(c_loc), &c_body) == 0);
  close(cfd[0]);
  close(cfd[1]);
  assert(c_status == 200);
  assert(c_body.len == 8u);
  assert(strcmp(c_body.data, "pure-c99") == 0);
  buffer_free(&c_body);

  /* Bodyless 304 Not Modified response on an open socket returns immediately */
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cfd) == 0);
  const char *mock_304_resp =
      "HTTP/1.1 304 Not Modified\r\n"
      "ETag: \"oyez-c99\"\r\n"
      "\r\n";
  assert(write(cfd[1], mock_304_resp, strlen(mock_304_resp)) == (ssize_t)strlen(mock_304_resp));
  buffer_init(&c_body);
  assert(http_client_exchange_fd(cfd[0], "example.com", 80u, 80, "/cached.xml",
                                 &c_status, c_loc, sizeof(c_loc), &c_body) == 0);
  close(cfd[0]);
  close(cfd[1]);
  assert(c_status == 304);
  assert(c_body.len == 0u);
  buffer_free(&c_body);
}

static void test_one_machine_consolidation_and_appliance(void) {
  OyezEngine eng;
  engine_init(&eng);

  uint32_t fid_wx = 0, fid_ai = 0;
  CatalogEntry c_wx = {"NOAA Hurricane Center", "https://nhc.noaa.example/rss", "https://nhc.noaa.example",
                       "weather", REGION_NORTH_AMERICA, "US", "North America", "en", "official", "Severe Weather"};
  CatalogEntry c_ai = {"Frontier AI Lab", "https://ai.example/rss", "https://ai.example",
                       "ai", REGION_NORTH_AMERICA, "US", "North America", "en", "specialist", "AI Models"};
  assert(engine_upsert_feed(&eng, &c_wx, &fid_wx) == 0);
  assert(engine_upsert_feed(&eng, &c_ai, &fid_ai) == 0);

  eng.feeds[0].fetch_count = 17u;
  eng.feeds[0].error_count = 2u;
  eng.feeds[0].last_fetched_ms = 1762140000000ULL;
  eng.feeds[1].fetch_count = 9u;
  eng.feeds[1].error_count = 0u;
  eng.feeds[1].last_fetched_ms = 1762141000000ULL;
  eng.total_bandit_pulls = 26ULL;
  eng.auto_poll_interval_sec = 90u;
  eng.last_auto_cycle_ms = 1762142000000ULL;
  eng.auto_cycles_completed = 5ULL;

  AutomationRule custom = {0};
  strcpy(custom.name, "typhoon-polar-vortex-watch");
  strcpy(custom.query, "super typhoon");
  strcpy(custom.category, "weather");
  custom.min_weather_severity = WEATHER_WARNING;
  custom.action = AUTO_ACTION_ALERT_RING;
  custom.enabled = 1;
  uint32_t custom_rid = 0;
  assert(engine_upsert_automation_rule(&eng, &custom, &custom_rid) == 0);
  assert(custom_rid >= 5u);

  int ins = 0;
  assert(engine_ingest_article(&eng,
                               fid_wx,
                               "wx-snap-1",
                               "Catastrophic Emergency: Super Typhoon & Category 5 Hurricane Storm Surge",
                               "https://nhc.noaa.example/typhoon-1",
                               "NHC Forecaster",
                               "Extreme hazard warning for super typhoon storm surge and flash flood emergency.",
                               "<p>Catastrophic super typhoon storm surge warning.</p>",
                               "2026-11-03T08:00:00Z",
                               1762150000000ULL,
                               "",
                               NULL,
                               0,
                               NULL,
                               &ins) == 0 && ins == 1);

  const char *snap_file = "/tmp/oyez_one_machine_test.ix";
  assert(engine_save_snapshot(&eng, snap_file) == 0);

  OyezEngine restored;
  engine_init(&restored);
  assert(engine_load_snapshot(&restored, snap_file) == 0);
  remove(snap_file);

  assert(restored.feed_count == 2u);
  assert(restored.article_count == 1u);
  assert(restored.feeds[0].fetch_count == 17u);
  assert(restored.feeds[0].error_count == 2u);
  assert(restored.feeds[0].last_fetched_ms == 1762140000000ULL);
  assert(restored.feeds[1].fetch_count == 9u);
  assert(restored.total_bandit_pulls == 26ULL);
  assert(restored.auto_poll_interval_sec == 90u);
  assert(restored.auto_cycles_completed == 5ULL);
  assert(restored.automation_rule_count == 6u);

  int found_custom = 0;
  for(size_t i = 0; i < restored.automation_rule_count; ++i) {
    if(strcmp(restored.automation_rules[i].name, "typhoon-polar-vortex-watch") == 0) {
      found_custom = 1;
      assert(restored.automation_rules[i].hit_count >= 1u);
      assert(restored.automation_rules[i].min_weather_severity == WEATHER_WARNING);
    }
  }
  assert(found_custom == 1);
  assert(restored.automation_alert_count >= 2u);

  EngineApplianceState app;
  engine_appliance_state(&restored, &app);
  assert(app.all_checks_passed == 1);
  assert(app.checks_passed == 12u);
  assert(app.checks_total == 12u);
  assert(app.all_theorems_verified == app.all_checks_passed);
  assert(app.theorems_verified_count == app.checks_passed);
  assert(app.max_numerical_residual < 1e-6);
  assert(app.total_resident_bytes > app.struct_base_bytes);
  assert(app.arena_bytes_used > 0u);
  assert(app.arena_bytes_capacity >= 65536u);
  assert(app.severe_weather_articles == 1u);
  assert(app.active_watchpoint_rules == 6u);
  assert(app.top_bandit_count == 2u);

  ServerContext ctx = {&restored, NULL, NULL, 0};
  int status = 0;
  Buffer out;
  assert(http_handle_request(&ctx, "GET", "/api/v1/appliance", "", 0, &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "\"appliance\":\"oyez-one-machine-c99\"") != NULL);
  assert(strstr(out.data, "\"status_deprecated\":true") != NULL);
  assert(strstr(out.data, "\"consistency_status\":\"passing\"") != NULL);
  assert(strstr(out.data, "\"all_checks_passed\":true") != NULL);
  assert(strstr(out.data, "\"checks_passed\":12") != NULL);
  assert(strstr(out.data, "\"all_theorems_verified\":true") != NULL);
  assert(strstr(out.data, "\"theorems_verified_count\":12") != NULL);
  assert(strstr(out.data, "\"total_resident_bytes\":") != NULL);
  assert(strstr(out.data, "\"cocone_adjunction_defect\":") != NULL);
  buffer_free(&out);

  engine_free(&restored);
  engine_free(&eng);
}

static void test_deep_information_retrieval_and_enrichment(void) {
  /* 1. JSON-LD structured extraction, \uXXXX Unicode escape decoding, <svg>/<template> stripping,
   *    and link-density / boilerplate candidate scoring in readability_extract */
  const char *rich_html =
      "<html><head>"
      "<meta name=\"citation_author\" content=\"Dr. Ada Lovelace\"/>"
      "<script type=\"application/ld+json\">"
      "{"
      "  \"@context\": \"https://schema.org\","
      "  \"@type\": \"NewsArticle\","
      "  \"headline\": \"Quantum Lattice \\u2014 AlphaFold \\u03b1 Breakthrough\","
      "  \"description\": \"Structured JSON-LD summary for deep retrieval.\","
      "  \"datePublished\": \"2026-11-03T14:30:00Z\","
      "  \"publisher\": {\"@type\": \"Organization\", \"name\": \"Oyez Science Press\"},"
      "  \"image\": {\"@type\": \"ImageObject\", \"url\": \"https://example.org/lead.png\"}"
      "}"
      "</script>"
      "</head><body>"
      "<div class=\"sidebar nav related\">"
      "<p><a href=\"/1\">Link one navigation item</a> <a href=\"/2\">Link two navigation item</a> "
      "<a href=\"/3\">Link three navigation item</a> <a href=\"/4\">Link four navigation item</a> "
      "<a href=\"/5\">Link five navigation item</a> <a href=\"/6\">Link six navigation item</a></p>"
      "</div>"
      "<div class=\"article-body\" itemprop=\"articleBody\">"
      "<svg><text>HIDDEN_SVG_NOISE</text></svg>"
      "<template><p>HIDDEN_TEMPLATE_NOISE</p></template>"
      "<p>Researchers demonstrated post-quantum lattice cryptography alongside FlashAttention-3 "
      "inference kernels on NVIDIA Blackwell GB200 clusters, achieving deterministic verification.</p>"
      "<p>The benchmark confirmed zero-knowledge proof soundness across distributed nodes, "
      "reducing latency while preserving full cryptographic integrity.</p>"
      "</div>"
      "</body></html>";

  ExtractedContent ex;
  assert(readability_extract(rich_html, strlen(rich_html), "https://example.org/paper", &ex) == 0);
  assert(strstr(ex.title, "Quantum Lattice") != NULL);
  assert(strstr(ex.title, "\xE2\x80\x94") != NULL); /* \u2014 decoded to UTF-8 em-dash */
  assert(strstr(ex.title, "\xCE\xB1") != NULL);     /* \u03b1 decoded to UTF-8 alpha */
  assert(strcmp(ex.byline, "Dr. Ada Lovelace") == 0);
  assert(strcmp(ex.site_name, "Oyez Science Press") == 0);
  assert(strcmp(ex.published_time, "2026-11-03T14:30:00Z") == 0);
  assert(strcmp(ex.lead_image_url, "https://example.org/lead.png") == 0);
  assert(strstr(ex.text_content, "HIDDEN_SVG_NOISE") == NULL);
  assert(strstr(ex.text_content, "HIDDEN_TEMPLATE_NOISE") == NULL);
  assert(strstr(ex.text_content, "Link one navigation") == NULL);
  assert(strstr(ex.text_content, "FlashAttention-3") != NULL);
  readability_free(&ex);

  /* 2. Atom nested <author><name> and JSON Feed 1.1 tags + external_url + \uXXXX */
  const char *atom_xml =
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">"
      "<title>ArXiv Cryptography</title>"
      "<entry>"
      "<id>urn:arxiv:2611.00001</id>"
      "<title>Lattice Signatures</title>"
      "<link href=\"https://arxiv.org/abs/2611.00001\"/>"
      "<author><name> Alan Turing </name><uri>https://arxiv.org/u/turing</uri></author>"
      "<prism:publicationDate>2026-11-03T12:00:00Z</prism:publicationDate>"
      "<dc:subject>cryptography</dc:subject>"
      "<dc:description> Post-quantum lattice trapdoors. </dc:description>"
      "</entry></feed>";
  ParsedFeed p_atom;
  assert(feed_parse(atom_xml, strlen(atom_xml), "https://arxiv.org", &p_atom) == 0);
  assert(p_atom.item_count == 1u);
  assert(strcmp(p_atom.items[0].author, "Alan Turing") == 0);
  assert(strcmp(p_atom.items[0].published_at, "2026-11-03T12:00:00Z") == 0);
  assert(p_atom.items[0].category_count == 1u);
  assert(strcmp(p_atom.items[0].categories[0], "cryptography") == 0);
  feed_free(&p_atom);

  const char *jfeed =
      "{\"version\":\"https://jsonfeed.org/version/1.1\",\"title\":\"AI Dispatch\","
      "\"items\":[{\"id\":\"jf-1\",\"title\":\"DeepSeek \\u2014 R1\",\"external_url\":\"https://ai.example/r1\","
      "\"author\":{\"name\":\"Grace Hopper\"},\"date_modified\":\"2026-11-03T15:00:00Z\","
      "\"content_text\":\"Open weights reasoning model.\",\"tags\":[\"ai\",\"semiconductors\"]}]}";
  ParsedFeed p_jf;
  assert(feed_parse(jfeed, strlen(jfeed), "https://ai.example", &p_jf) == 0);
  assert(p_jf.item_count == 1u);
  assert(strstr(p_jf.items[0].title, "\xE2\x80\x94") != NULL);
  assert(strcmp(p_jf.items[0].url, "https://ai.example/r1") == 0);
  assert(strcmp(p_jf.items[0].author, "Grace Hopper") == 0);
  assert(strcmp(p_jf.items[0].published_at, "2026-11-03T15:00:00Z") == 0);
  assert(p_jf.items[0].category_count == 2u);
  assert(strcmp(p_jf.items[0].categories[0], "ai") == 0);
  assert(strcmp(p_jf.items[0].categories[1], "semiconductors") == 0);
  feed_free(&p_jf);

  /* 3. Deep 32 KB window entity extraction (past old 4 KB truncation limit) */
  char *long_doc = malloc(16384u);
  assert(long_doc != NULL);
  memset(long_doc, ' ', 16384u);
  for(size_t i = 0; i < 12000u; i += 10u) {
    memcpy(long_doc + i, "baseline. ", 10u);
  }
  const char *tail_payload =
      "At offset 12KB we disclose CVE-2026-7788 affecting deepseek-ai/DeepSeek-R1-Zero "
      "and NVIDIA Blackwell GB200 clusters monitored by the Federal Reserve in Japan.";
  memcpy(long_doc + 12000u, tail_payload, strlen(tail_payload));
  long_doc[12000u + strlen(tail_payload)] = '\0';

  ExtractedEntities deep_ent;
  entities_extract("Long-form Technical Dispatch", "Initial abstract.", long_doc,
                   "public-affairs", NULL, 0, &deep_ent);
  assert(deep_ent.cve_count == 1u);
  assert(strcmp(deep_ent.cves[0], "CVE-2026-7788") == 0);
  assert(deep_ent.hf_repo_count == 1u);
  assert(strcmp(deep_ent.hf_repos[0], "deepseek-ai/deepseek-r1-zero") == 0);
  assert(deep_ent.ai_infra_count >= 1u);
  assert(deep_ent.org_count >= 1u);
  assert(deep_ent.country_count >= 1u);
  free(long_doc);

  /* 4. Compound token sub-part search, canonical entity ticker search,
   *    in-place article enrichment, and query-focused match_snippet */
  OyezEngine eng;
  engine_init(&eng);
  CatalogEntry cat = {"Research Wire", "https://wire.example/rss", "https://wire.example",
                      "technology", REGION_NORTH_AMERICA, "US", "North America", "en", "official", "Tech"};
  uint32_t fid = 0;
  assert(engine_upsert_feed(&eng, &cat, &fid) == 0);

  uint32_t stub_id = 0;
  int ins = 0;
  assert(engine_ingest_article(&eng,
                               fid,
                               "stub-guid-1",
                               "Hardware & Treasury Market Brief",
                               "https://wire.example/articles/stub-1",
                               "",
                               "Short teaser stub.",
                               "<p>Short teaser stub.</p>",
                               "2026-11-03T16:00:00Z",
                               1762185600000ULL,
                               "",
                               NULL,
                               0,
                               &stub_id,
                               &ins) == 0 && ins == 1);

  /* Enrich the stub article via POST /api/v1/articles/:id/enrich with full HTML body */
  ServerContext ctx = {&eng, NULL, NULL, 0};
  int status = 0;
  Buffer out;
  char enrich_path[64];
  snprintf(enrich_path, sizeof(enrich_path), "/api/v1/articles/%u/enrich", stub_id);
  const char *enrich_body =
      "{\"html\":\"<html><head><meta name='author' content='Dr. Richard Feynman'/></head>"
      "<body><article class='article-body'>"
      "<p>Initial overview of global semiconductor supply chains and sovereign bond markets.</p>"
      "<p>Deep in the report, NVIDIA Blackwell architecture and 10-year yield movements "
      "coincide with patches for CVE-2026-4242 across DeepSeek-R1 inference clusters.</p>"
      "</article></body></html>\"}";
  assert(http_handle_request(&ctx, "POST", enrich_path, enrich_body, strlen(enrich_body), &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "\"status\":\"enriched\"") != NULL);
  assert(strstr(out.data, "\"upgraded\":true") != NULL);
  assert(strstr(out.data, "\"CVE-2026-4242\"") != NULL);
  assert(strstr(out.data, "\"NVDA\"") != NULL);
  assert(strstr(out.data, "\"UST10Y\"") != NULL);
  buffer_free(&out);

  /* Canonical ticker search (q=NVDA and q=UST10Y) and compound sub-part search (q=4242) */
  assert(http_handle_request(&ctx, "GET", "/api/v1/articles?q=NVDA", "", 0, &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "\"total\":1") != NULL);
  assert(strstr(out.data, "\"match_snippet\":") != NULL);
  buffer_free(&out);

  assert(http_handle_request(&ctx, "GET", "/api/v1/articles?q=4242", "", 0, &status, &out) == 0);
  assert(status == 200);
  assert(strstr(out.data, "\"total\":1") != NULL);
  assert(strstr(out.data, "CVE-2026-4242") != NULL);
  buffer_free(&out);

  engine_free(&eng);
}

int main(void) {
  test_checksum_and_wire();
  test_bpe_and_m1_tokenizer();
  test_markup_and_readability();
  test_category_theory_and_ontology();
  test_engine_search_and_categorical_endpoints();
  test_hardening_and_concurrency();
  test_http_connection_framing();
  test_url_fetch_redirect_policy();
  test_one_machine_consolidation_and_appliance();
  test_deep_information_retrieval_and_enrichment();
  printf("ALL C99 OYEZ TESTS PASSED\n");
  return 0;
}
