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
  assert(m1_tokenizer_train(&m1, (const uint8_t *)corpus, strlen(corpus), 460u) == 0);
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
  /* Transitivity: ai <= technology <= knowledge-economy <= all-news */
  assert(ontology_has_morphism(ONTO_AI, ONTO_TECHNOLOGY) == 1);
  assert(ontology_has_morphism(ONTO_TECHNOLOGY, ONTO_KNOWLEDGE_ECONOMY) == 1);
  assert(ontology_has_morphism(ONTO_AI, ONTO_KNOWLEDGE_ECONOMY) == 1);

  /* Join & Meet: cyber <= security and cyber <= technology */
  uint32_t meet = ontology_meet_mask(ONTO_SECURITY, ONTO_TECHNOLOGY);
  assert((meet & (1u << ONTO_CYBER)) != 0u);
  OntologyNodeId join = ontology_join(ONTO_AI, ONTO_SPACE);
  assert(join == ONTO_SCIENCE || join == ONTO_TECHNOLOGY || join == ONTO_KNOWLEDGE_ECONOMY);

  /* Left Kan Extension */
  double raw[ONTOLOGY_NODE_COUNT] = {0};
  double kan[ONTOLOGY_NODE_COUNT] = {0};
  raw[ONTO_AI] = 10.0;
  raw[ONTO_CYBER] = 6.0;
  ontology_left_kan_extend(raw, kan);
  assert(kan[ONTO_TECHNOLOGY] > 10.0);
  assert(kan[ONTO_SECURITY] > 3.0);
  assert(kan[ONTO_ROOT] >= kan[ONTO_TECHNOLOGY]);

  /* Epistemic NewsType classification */
  assert(news_type_classify("BREAKING: Earthquake strikes Pacific trench", "", "", "climate") ==
         NEWS_TYPE_BREAKING_DISPATCH);
  assert(news_type_classify("Leaked documents uncovered in whistleblower investigation", "", "", "law") ==
         NEWS_TYPE_INVESTIGATIVE_REPORT);
  assert(news_type_classify("Central bank raises interest rate by 50 basis points", "", "", "economy") ==
         NEWS_TYPE_MARKET_AND_ECONOMIC);
}

static void test_engine_search_and_categorical_endpoints(void) {
  OyezEngine eng;
  engine_init(&eng);

  size_t seeded = engine_seed_catalog(&eng, NULL, NULL);
  assert(seeded >= 240u);

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

  /* Formal Concept Analysis */
  FormalConcept concepts[16];
  size_t nc = engine_formal_concepts(&eng, 1u, concepts, 16u);
  assert(nc >= 2u);

  /* Bipartite PageRank */
  RankedEntity ents[16];
  ScoredArticle top_a[8];
  size_t ne = engine_bipartite_pagerank(&eng, 12u, 0.85, ents, 16u, top_a, 8u);
  assert(ne >= 2u);

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
  buffer_free(&json);

  m1_tokenizer_free(&tok);
  engine_free(&loaded);
  engine_free(&eng);
}

int main(void) {
  test_checksum_and_wire();
  test_bpe_and_m1_tokenizer();
  test_markup_and_readability();
  test_category_theory_and_ontology();
  test_engine_search_and_categorical_endpoints();
  printf("ALL C23 OYEZ TESTS PASSED\n");
  return 0;
}
