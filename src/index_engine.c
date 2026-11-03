#include "index_engine.h"

#include "checksum.h"
#include "markup.h"
#include "wire.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
  if(!s) s = "";
  size_t len = strlen(s);
  char *out = malloc(len + 1u);
  if(!out) return NULL;
  memcpy(out, s, len + 1u);
  return out;
}

void bitset_init(BitSet *bs) {
  memset(bs, 0, sizeof(*bs));
}

void bitset_free(BitSet *bs) {
  if(!bs) return;
  free(bs->words);
  memset(bs, 0, sizeof(*bs));
}

static int bitset_ensure(BitSet *bs, size_t needed_words) {
  if(needed_words <= bs->word_cap) return 0;
  size_t next = bs->word_cap ? bs->word_cap : 8u;
  while(next < needed_words) next *= 2u;
  uint64_t *grown = realloc(bs->words, next * sizeof(uint64_t));
  if(!grown) return -1;
  memset(grown + bs->word_cap, 0, (next - bs->word_cap) * sizeof(uint64_t));
  bs->words = grown;
  bs->word_cap = next;
  return 0;
}

int bitset_set(BitSet *bs, uint32_t bit) {
  size_t w = (size_t)(bit >> 6u);
  if(bitset_ensure(bs, w + 1u) != 0) return -1;
  bs->words[w] |= (1ULL << (bit & 63u));
  if(bit + 1u > bs->max_slot) bs->max_slot = bit + 1u;
  return 0;
}

int bitset_test(const BitSet *bs, uint32_t bit) {
  size_t w = (size_t)(bit >> 6u);
  if(!bs || w >= bs->word_cap) return 0;
  return (bs->words[w] & (1ULL << (bit & 63u))) != 0ULL;
}

uint32_t bitset_popcount(const BitSet *bs) {
  if(!bs || !bs->words) return 0u;
  uint32_t total = 0u;
  for(size_t i = 0; i < bs->word_cap; ++i) {
    total += popcnt64(bs->words[i]);
  }
  return total;
}

int bitset_clone(const BitSet *src, BitSet *dst) {
  bitset_init(dst);
  if(!src || src->word_cap == 0) return 0;
  if(bitset_ensure(dst, src->word_cap) != 0) return -1;
  memcpy(dst->words, src->words, src->word_cap * sizeof(uint64_t));
  dst->max_slot = src->max_slot;
  return 0;
}

void bitset_and_inplace(BitSet *dst, const BitSet *other) {
  if(!dst) return;
  if(!other || !other->words) {
    if(dst->words) memset(dst->words, 0, dst->word_cap * sizeof(uint64_t));
    return;
  }
  size_t min_w = dst->word_cap < other->word_cap ? dst->word_cap : other->word_cap;
  for(size_t i = 0; i < min_w; ++i) {
    dst->words[i] &= other->words[i];
  }
  for(size_t i = min_w; i < dst->word_cap; ++i) {
    dst->words[i] = 0ULL;
  }
}

int bitset_or_inplace(BitSet *dst, const BitSet *other) {
  if(!dst || !other || other->word_cap == 0) return 0;
  if(bitset_ensure(dst, other->word_cap) != 0) return -1;
  for(size_t i = 0; i < other->word_cap; ++i) {
    dst->words[i] |= other->words[i];
  }
  if(other->max_slot > dst->max_slot) dst->max_slot = other->max_slot;
  return 0;
}

static BitSet *country_fiber_get(CountryFiber *table, size_t cap, const char *code, int create) {
  if(!code || code[0] == '\0') return NULL;
  char norm[8];
  size_t k = 0;
  while(code[k] && k < 7u) {
    norm[k] = (char)toupper((unsigned char)code[k]);
    k++;
  }
  norm[k] = '\0';
  uint64_t h = fnv1a64(norm, k);
  size_t mask = cap - 1u;
  size_t idx = (size_t)h & mask;
  for(size_t step = 0; step < cap; ++step) {
    CountryFiber *slot = &table[(idx + step) & mask];
    if(!slot->occupied) {
      if(!create) return NULL;
      slot->occupied = 1;
      strcpy(slot->key, norm);
      bitset_init(&slot->bits);
      return &slot->bits;
    }
    if(strcmp(slot->key, norm) == 0) {
      return &slot->bits;
    }
  }
  return NULL;
}

static int inv_table_grow(OyezEngine *engine) {
  size_t next_cap = engine->inv_cap ? (engine->inv_cap * 2u) : 4096u;
  InvertedBucket *next = calloc(next_cap, sizeof(InvertedBucket));
  if(!next) return -1;
  size_t mask = next_cap - 1u;
  for(size_t i = 0; i < engine->inv_cap; ++i) {
    if(engine->inv_table[i].occupied) {
      InvertedBucket b = engine->inv_table[i];
      size_t idx = (size_t)b.hash & mask;
      while(next[idx].occupied) idx = (idx + 1u) & mask;
      next[idx] = b;
    }
  }
  free(engine->inv_table);
  engine->inv_table = next;
  engine->inv_cap = next_cap;
  return 0;
}

static int inv_post(OyezEngine *engine, const char *term, uint64_t hash, uint32_t slot, float weight) {
  if((engine->inv_count + 1u) * 10u >= engine->inv_cap * 7u) {
    if(inv_table_grow(engine) != 0) return -1;
  }
  size_t mask = engine->inv_cap - 1u;
  size_t idx = (size_t)hash & mask;
  while(engine->inv_table[idx].occupied) {
    if(engine->inv_table[idx].hash == hash && strcmp(engine->inv_table[idx].term, term) == 0) {
      InvertedBucket *b = &engine->inv_table[idx];
      if(b->count == b->cap) {
        uint32_t ncap = b->cap ? (b->cap * 2u) : 8u;
        uint32_t *nslots = realloc(b->slots, ncap * sizeof(uint32_t));
        float *nw = realloc(b->weights, ncap * sizeof(float));
        if(!nslots || !nw) {
          if(nslots) b->slots = nslots;
          if(nw) b->weights = nw;
          return -1;
        }
        b->slots = nslots;
        b->weights = nw;
        b->cap = ncap;
      }
      b->slots[b->count] = slot;
      b->weights[b->count] = weight;
      b->count++;
      return 0;
    }
    idx = (idx + 1u) & mask;
  }

  InvertedBucket *b = &engine->inv_table[idx];
  b->occupied = 1;
  strncpy(b->term, term, 31u);
  b->term[31] = '\0';
  b->hash = hash;
  b->cap = 8u;
  b->slots = malloc(b->cap * sizeof(uint32_t));
  b->weights = malloc(b->cap * sizeof(float));
  if(!b->slots || !b->weights) return -1;
  b->slots[0] = slot;
  b->weights[0] = weight;
  b->count = 1u;
  engine->inv_count++;
  return 0;
}

static const InvertedBucket *inv_lookup(const OyezEngine *engine, const char *term, uint64_t hash) {
  if(!engine || !engine->inv_table || engine->inv_cap == 0) return NULL;
  size_t mask = engine->inv_cap - 1u;
  size_t idx = (size_t)hash & mask;
  for(size_t step = 0; step < engine->inv_cap; ++step) {
    const InvertedBucket *b = &engine->inv_table[(idx + step) & mask];
    if(!b->occupied) return NULL;
    if(b->hash == hash && strcmp(b->term, term) == 0) return b;
  }
  return NULL;
}

void engine_init(OyezEngine *engine) {
  ontology_init();
  memset(engine, 0, sizeof(*engine));
  engine->next_feed_id = 1u;
  engine->next_article_id = 1u;
  inv_table_grow(engine);
}

static void free_feed_record(FeedRecord *f) {
  free(f->title);
  free(f->feed_url);
  free(f->site_url);
  free(f->description);
  free(f->category);
  free(f->subregion);
  free(f->language);
  free(f->source_tier);
  free(f->editorial_focus);
  free(f->last_error);
}

static void free_article_record(ArticleRecord *a) {
  free(a->guid);
  free(a->title);
  free(a->url);
  free(a->author);
  free(a->summary);
  free(a->content_text);
  free(a->content_html);
  free(a->published_at);
  free(a->image_url);
  free(a->feed_title);
  free(a->feed_category);
  free(a->feed_language);
  free(a->feed_source_tier);
}

void engine_free(OyezEngine *engine) {
  if(!engine) return;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    free_feed_record(&engine->feeds[i]);
  }
  free(engine->feeds);

  for(size_t i = 0; i < engine->article_count; ++i) {
    free_article_record(&engine->articles[i]);
  }
  free(engine->articles);

  bitset_free(&engine->all_articles);
  for(size_t i = 0; i < WORLD_REGION_COUNT; ++i) bitset_free(&engine->by_region[i]);
  for(size_t i = 0; i < NEWS_TYPE_COUNT; ++i) bitset_free(&engine->by_news_type[i]);
  for(size_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    bitset_free(&engine->by_direct_ontology[i]);
    bitset_free(&engine->by_closure_ontology[i]);
  }
  for(size_t i = 0; i < 128u; ++i) {
    if(engine->by_source_country[i].occupied) bitset_free(&engine->by_source_country[i].bits);
    if(engine->by_mentioned_country[i].occupied) bitset_free(&engine->by_mentioned_country[i].bits);
  }
  for(size_t i = 0; i < engine->inv_cap; ++i) {
    if(engine->inv_table[i].occupied) {
      free(engine->inv_table[i].slots);
      free(engine->inv_table[i].weights);
    }
  }
  free(engine->inv_table);
  memset(engine, 0, sizeof(*engine));
}

int engine_upsert_feed(OyezEngine *engine, const CatalogEntry *entry, uint32_t *out_feed_id) {
  if(!engine || !entry || !entry->feed_url) return -1;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    if(strcmp(engine->feeds[i].feed_url, entry->feed_url) == 0) {
      if(out_feed_id) *out_feed_id = engine->feeds[i].id;
      return 0;
    }
  }

  if(engine->feed_count == engine->feed_cap) {
    size_t ncap = engine->feed_cap ? (engine->feed_cap * 2u) : 128u;
    FeedRecord *grown = realloc(engine->feeds, ncap * sizeof(FeedRecord));
    if(!grown) return -1;
    engine->feeds = grown;
    engine->feed_cap = ncap;
  }

  FeedRecord *f = &engine->feeds[engine->feed_count++];
  memset(f, 0, sizeof(*f));
  f->id = engine->next_feed_id++;
  f->title = dup_str(entry->title ? entry->title : entry->feed_url);
  f->feed_url = dup_str(entry->feed_url);
  f->site_url = dup_str(entry->site_url ? entry->site_url : entry->feed_url);
  f->description = dup_str(entry->editorial_focus ? entry->editorial_focus : "");
  f->category = dup_str(entry->category ? entry->category : "public-affairs");
  f->region = entry->region;
  strncpy(f->country, entry->country ? entry->country : "INT", 3u);
  f->country[3] = '\0';
  f->subregion = dup_str(entry->subregion ? entry->subregion : "");
  f->language = dup_str(entry->language ? entry->language : "en");
  f->source_tier = dup_str(entry->source_tier ? entry->source_tier : "independent");
  f->editorial_focus = dup_str(entry->editorial_focus ? entry->editorial_focus : "");
  f->last_error = dup_str("");
  if(out_feed_id) *out_feed_id = f->id;
  return 0;
}

const FeedRecord *engine_find_feed(const OyezEngine *engine, uint32_t feed_id) {
  if(!engine) return NULL;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    if(engine->feeds[i].id == feed_id) return &engine->feeds[i];
  }
  return NULL;
}

const FeedRecord *engine_find_feed_by_url(const OyezEngine *engine, const char *feed_url) {
  if(!engine || !feed_url) return NULL;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    if(strcmp(engine->feeds[i].feed_url, feed_url) == 0) return &engine->feeds[i];
  }
  return NULL;
}

size_t engine_seed_catalog(OyezEngine *engine, const char *region_slug, const char *country_code) {
  size_t count = 0;
  const CatalogEntry *entries = catalog_entries(&count);
  size_t added = 0;
  WorldRegion filter_region = REGION_GLOBAL;
  int has_region = (region_slug && region_slug[0] != '\0' && world_region_from_slug(region_slug, &filter_region) == 0);

  for(size_t i = 0; i < count; ++i) {
    if(has_region && entries[i].region != filter_region) continue;
    if(country_code && country_code[0] != '\0' && strcmp(entries[i].country, country_code) != 0) continue;
    size_t before = engine->feed_count;
    if(engine_upsert_feed(engine, &entries[i], NULL) == 0 && engine->feed_count > before) {
      added++;
    }
  }
  return added;
}

static void index_article_record(OyezEngine *engine, ArticleRecord *a) {
  uint32_t slot = a->slot;
  bitset_set(&engine->all_articles, slot);
  if((unsigned)a->feed_region < WORLD_REGION_COUNT) {
    bitset_set(&engine->by_region[a->feed_region], slot);
  }
  if((unsigned)a->news_type < NEWS_TYPE_COUNT) {
    bitset_set(&engine->by_news_type[a->news_type], slot);
  }
  for(uint32_t node = 0; node < ONTOLOGY_NODE_COUNT; ++node) {
    if(a->direct_ontology_mask & (1u << node)) {
      bitset_set(&engine->by_direct_ontology[node], slot);
    }
    if(a->closure_ontology_mask & (1u << node)) {
      bitset_set(&engine->by_closure_ontology[node], slot);
    }
  }

  BitSet *src_cb = country_fiber_get(engine->by_source_country, 128u, a->feed_country, 1);
  if(src_cb) bitset_set(src_cb, slot);

  for(size_t i = 0; i < a->mentioned_country_count; ++i) {
    BitSet *m_cb = country_fiber_get(engine->by_mentioned_country, 128u, a->mentioned_countries[i], 1);
    if(m_cb) bitset_set(m_cb, slot);
  }

  LexicalTerm terms[MAX_LEXICAL_TOKENS];
  size_t term_count = 0;
  uint32_t doc_tokens = 0;
  term_count = lexical_analyze(a->title, 3.2f, terms, term_count, MAX_LEXICAL_TOKENS, &doc_tokens);
  term_count = lexical_analyze(a->summary, 1.8f, terms, term_count, MAX_LEXICAL_TOKENS, &doc_tokens);
  term_count = lexical_analyze(a->content_text, 1.0f, terms, term_count, MAX_LEXICAL_TOKENS, &doc_tokens);
  term_count = lexical_analyze(a->feed_title, 1.4f, terms, term_count, MAX_LEXICAL_TOKENS, &doc_tokens);

  a->doc_len = doc_tokens > 0 ? doc_tokens : 1u;
  engine->total_doc_len += a->doc_len;
  a->simhash64 = simhash64_compute(terms, term_count);

  for(size_t i = 0; i < term_count; ++i) {
    inv_post(engine, terms[i].term, terms[i].hash, slot, terms[i].weight);
  }
}

int engine_ingest_article(OyezEngine *engine,
                          uint32_t feed_id,
                          const char *guid,
                          const char *title,
                          const char *url,
                          const char *author,
                          const char *summary,
                          const char *content_html,
                          const char *published_at,
                          uint64_t published_ms,
                          const char *image_url,
                          const char *const *tags,
                          size_t tag_count,
                          uint32_t *out_article_id,
                          int *out_inserted) {
  if(!engine || !title) return -1;
  if(out_inserted) *out_inserted = 0;

  FeedRecord *feed = NULL;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    if(engine->feeds[i].id == feed_id) {
      feed = &engine->feeds[i];
      break;
    }
  }
  if(!feed) return -1;

  const char *eff_guid = (guid && guid[0]) ? guid : ((url && url[0]) ? url : title);
  for(size_t i = 0; i < engine->article_count; ++i) {
    if(engine->articles[i].feed_id == feed_id && strcmp(engine->articles[i].guid, eff_guid) == 0) {
      if(out_article_id) *out_article_id = engine->articles[i].id;
      return 0;
    }
    if(url && url[0] && strcmp(engine->articles[i].url, url) == 0) {
      if(out_article_id) *out_article_id = engine->articles[i].id;
      return 0;
    }
  }

  if(engine->article_count == engine->article_cap) {
    size_t ncap = engine->article_cap ? (engine->article_cap * 2u) : 256u;
    ArticleRecord *grown = realloc(engine->articles, ncap * sizeof(ArticleRecord));
    if(!grown) return -1;
    engine->articles = grown;
    engine->article_cap = ncap;
  }

  uint32_t slot = (uint32_t)engine->article_count;
  ArticleRecord *a = &engine->articles[engine->article_count++];
  memset(a, 0, sizeof(*a));
  a->id = engine->next_article_id++;
  a->slot = slot;
  a->feed_id = feed_id;
  a->guid = dup_str(eff_guid);
  a->title = dup_str(title);
  a->url = dup_str(url ? url : "");
  a->author = dup_str(author ? author : "");
  a->summary = dup_str(summary ? summary : "");
  a->content_html = dup_str(content_html ? content_html : "");
  a->content_text = markup_clean_cstr((content_html && content_html[0]) ? content_html : summary);
  a->published_at = dup_str(published_at ? published_at : "2026-11-03T00:00:00Z");
  a->published_ms = published_ms > 0 ? published_ms : wall_time_ms();
  a->image_url = dup_str(image_url ? image_url : "");
  a->feed_title = dup_str(feed->title);
  a->feed_category = dup_str(feed->category);
  a->feed_region = feed->region;
  strcpy(a->feed_country, feed->country);
  a->feed_language = dup_str(feed->language);
  a->feed_source_tier = dup_str(feed->source_tier);

  ExtractedEntities ent;
  entities_extract(a->title, a->summary, a->content_text, a->feed_category, tags, tag_count, &ent);
  a->news_type = ent.news_type;
  a->direct_ontology_mask = ent.direct_ontology_mask;
  a->closure_ontology_mask = ent.closure_ontology_mask;

  /* Always include feed country if valid 2-letter ISO code */
  if(strlen(a->feed_country) == 2u && strcmp(a->feed_country, "UN") != 0 &&
     strcmp(a->feed_country, "EU") != 0) {
    strcpy(a->mentioned_countries[a->mentioned_country_count++], a->feed_country);
  }
  for(size_t i = 0; i < ent.country_count && a->mentioned_country_count < MAX_EXTRACTED_COUNTRIES; ++i) {
    int exists = 0;
    for(size_t j = 0; j < a->mentioned_country_count; ++j) {
      if(strcmp(a->mentioned_countries[j], ent.countries[i]) == 0) {
        exists = 1;
        break;
      }
    }
    if(!exists) {
      strcpy(a->mentioned_countries[a->mentioned_country_count++], ent.countries[i]);
    }
  }
  for(size_t i = 0; i < ent.org_count && a->mentioned_org_count < MAX_EXTRACTED_ORGS; ++i) {
    strcpy(a->mentioned_orgs[a->mentioned_org_count++], ent.organizations[i]);
  }

  uint32_t words = 0;
  int in_w = 0;
  for(const char *p = a->content_text; *p; ++p) {
    if(isspace((unsigned char)*p)) in_w = 0;
    else if(!in_w) {
      in_w = 1;
      words++;
    }
  }
  a->word_count = words;
  a->reading_time_minutes = words > 0 ? ((words + 219u) / 220u) : 1u;

  index_article_record(engine, a);
  feed->article_count++;

  if(out_article_id) *out_article_id = a->id;
  if(out_inserted) *out_inserted = 1;
  return 0;
}

const ArticleRecord *engine_get_article_by_id(const OyezEngine *engine, uint32_t id) {
  if(!engine) return NULL;
  for(size_t i = 0; i < engine->article_count; ++i) {
    if(engine->articles[i].id == id) return &engine->articles[i];
  }
  return NULL;
}

static int compare_scored_desc(const void *a, const void *b) {
  const ScoredArticle *sa = (const ScoredArticle *)a;
  const ScoredArticle *sb = (const ScoredArticle *)b;
  if(sb->score > sa->score) return 1;
  if(sb->score < sa->score) return -1;
  if(sb->article->published_ms > sa->article->published_ms) return 1;
  if(sb->article->published_ms < sa->article->published_ms) return -1;
  return 0;
}

static double article_similarity(const ArticleRecord *a, const ArticleRecord *b) {
  uint32_t hamming = simhash64_distance(a->simhash64, b->simhash64);
  double sim_hash = 1.0 - ((double)hamming / 64.0);
  uint32_t inter_onto = popcnt64((uint64_t)(a->closure_ontology_mask & b->closure_ontology_mask));
  uint32_t union_onto = popcnt64((uint64_t)(a->closure_ontology_mask | b->closure_ontology_mask));
  double sim_onto = union_onto > 0 ? ((double)inter_onto / (double)union_onto) : 0.0;
  double same_feed = (a->feed_id == b->feed_id) ? 1.0 : 0.0;
  return 0.50 * sim_hash + 0.30 * sim_onto + 0.20 * same_feed;
}

int engine_search(const OyezEngine *engine, const ArticleQuery *q, SearchResponse *out) {
  if(!engine || !q || !out) return -1;
  memset(out, 0, sizeof(*out));
  uint64_t t0 = monotonic_ns();

  BitSet candidates;
  if(bitset_clone(&engine->all_articles, &candidates) != 0) return -1;

  if(q->region && q->region[0]) {
    WorldRegion r;
    if(world_region_from_slug(q->region, &r) == 0) {
      bitset_and_inplace(&candidates, &engine->by_region[r]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->news_type && q->news_type[0]) {
    NewsType nt;
    if(news_type_from_slug(q->news_type, &nt) == 0) {
      bitset_and_inplace(&candidates, &engine->by_news_type[nt]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->category && q->category[0]) {
    OntologyNodeId node;
    if(ontology_from_slug(q->category, &node) == 0) {
      if(q->use_category_morphism) {
        bitset_and_inplace(&candidates, &engine->by_closure_ontology[node]);
      } else {
        bitset_and_inplace(&candidates, &engine->by_direct_ontology[node]);
      }
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->country && q->country[0]) {
    const BitSet *cb = country_fiber_get((CountryFiber *)engine->by_source_country, 128u, q->country, 0);
    if(cb) bitset_and_inplace(&candidates, cb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->mentioned_country && q->mentioned_country[0]) {
    const BitSet *mcb = country_fiber_get((CountryFiber *)engine->by_mentioned_country, 128u, q->mentioned_country, 0);
    if(mcb) bitset_and_inplace(&candidates, mcb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  LexicalTerm qterms[32];
  size_t qterm_count = 0;
  if(q->query && q->query[0]) {
    qterm_count = lexical_analyze(q->query, 1.0f, qterms, 0, 32u, NULL);
  }

  double *bm25_scores = NULL;
  if(qterm_count > 0 && engine->article_count > 0) {
    bm25_scores = calloc(engine->article_count, sizeof(double));
    if(!bm25_scores) {
      bitset_free(&candidates);
      return -1;
    }
    BitSet query_union;
    bitset_init(&query_union);
    double N = (double)engine->article_count;
    double avgdl = engine->total_doc_len > 0 ? ((double)engine->total_doc_len / N) : 100.0;
    const double k1 = 1.2;
    const double b_param = 0.75;

    for(size_t t = 0; t < qterm_count; ++t) {
      const InvertedBucket *bucket = inv_lookup(engine, qterms[t].term, qterms[t].hash);
      if(!bucket || bucket->count == 0) continue;
      double df = (double)bucket->count;
      double idf = log(1.0 + (N - df + 0.5) / (df + 0.5));
      for(uint32_t p = 0; p < bucket->count; ++p) {
        uint32_t slot = bucket->slots[p];
        if(!bitset_test(&candidates, slot)) continue;
        double tf = (double)bucket->weights[p];
        double dl = (double)engine->articles[slot].doc_len;
        double denom = tf + k1 * (1.0 - b_param + b_param * (dl / avgdl));
        bm25_scores[slot] += idf * ((tf * (k1 + 1.0)) / denom);
        bitset_set(&query_union, slot);
      }
    }
    bitset_and_inplace(&candidates, &query_union);
    bitset_free(&query_union);
  }

  uint32_t matched_cap = bitset_popcount(&candidates);
  if(matched_cap == 0) {
    free(bm25_scores);
    bitset_free(&candidates);
    out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
    return 0;
  }

  ScoredArticle *pool = malloc(matched_cap * sizeof(ScoredArticle));
  if(!pool) {
    free(bm25_scores);
    bitset_free(&candidates);
    return -1;
  }

  size_t pool_count = 0;
  for(size_t w = 0; w < candidates.word_cap; ++w) {
    uint64_t word = candidates.words[w];
    while(word) {
      uint32_t bit = ctz64(word);
      uint32_t slot = (uint32_t)(w * 64u + bit);
      word &= (word - 1ULL);
      if(slot >= engine->article_count) continue;
      const ArticleRecord *art = &engine->articles[slot];
      if(q->feed_id > 0 && art->feed_id != q->feed_id) continue;
      if(q->language && q->language[0] && strcmp(art->feed_language, q->language) != 0) continue;
      if(q->source_tier && q->source_tier[0] && strcmp(art->feed_source_tier, q->source_tier) != 0) continue;

      double sc = bm25_scores ? bm25_scores[slot] : (double)(art->published_ms % 100000000ULL) * 1e-6 + 1.0;
      pool[pool_count].article = art;
      pool[pool_count].score = sc;
      pool_count++;
    }
  }

  free(bm25_scores);
  bitset_free(&candidates);

  qsort(pool, pool_count, sizeof(ScoredArticle), compare_scored_desc);

  size_t limit = q->limit > 0 ? q->limit : 50u;
  size_t offset = q->offset;
  out->total = pool_count;

  if(offset >= pool_count) {
    free(pool);
    out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
    return 0;
  }

  if(q->diversify_mmr && pool_count > 1u) {
    size_t window = pool_count < 120u ? pool_count : 120u;
    double max_rel = pool[0].score > 0.0 ? pool[0].score : 1.0;
    double lambda = (q->mmr_lambda > 0.0 && q->mmr_lambda <= 1.0) ? q->mmr_lambda : 0.65;
    uint8_t *used = calloc(window, 1u);
    ScoredArticle *reranked = malloc(window * sizeof(ScoredArticle));
    if(used && reranked) {
      reranked[0] = pool[0];
      used[0] = 1u;
      size_t rcount = 1u;
      while(rcount < window) {
        size_t best_idx = 0;
        double best_mmr = -1e18;
        for(size_t i = 0; i < window; ++i) {
          if(used[i]) continue;
          double norm_rel = pool[i].score / max_rel;
          double max_sim = 0.0;
          for(size_t s = 0; s < rcount; ++s) {
            double sim = article_similarity(pool[i].article, reranked[s].article);
            if(sim > max_sim) max_sim = sim;
          }
          double mmr = lambda * norm_rel - (1.0 - lambda) * max_sim;
          if(mmr > best_mmr) {
            best_mmr = mmr;
            best_idx = i;
          }
        }
        used[best_idx] = 1u;
        reranked[rcount++] = pool[best_idx];
      }
      memcpy(pool, reranked, window * sizeof(ScoredArticle));
    }
    free(used);
    free(reranked);
  }

  size_t avail = pool_count - offset;
  size_t take = avail < limit ? avail : limit;
  out->hits = malloc(take * sizeof(ScoredArticle));
  if(!out->hits) {
    free(pool);
    return -1;
  }
  memcpy(out->hits, pool + offset, take * sizeof(ScoredArticle));
  out->count = take;
  free(pool);
  out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
  return 0;
}

void search_response_free(SearchResponse *res) {
  if(!res) return;
  free(res->hits);
  memset(res, 0, sizeof(*res));
}

size_t engine_find_near_duplicates(const OyezEngine *engine,
                                   uint32_t article_id,
                                   uint32_t max_hamming,
                                   const ArticleRecord **out_articles,
                                   uint32_t *out_distances,
                                   size_t max_out) {
  const ArticleRecord *target = engine_get_article_by_id(engine, article_id);
  if(!target || !out_articles || max_out == 0) return 0;
  size_t found = 0;
  for(size_t i = 0; i < engine->article_count; ++i) {
    const ArticleRecord *cand = &engine->articles[i];
    if(cand->id == target->id) continue;
    uint32_t dist = simhash64_distance(target->simhash64, cand->simhash64);
    if(dist <= max_hamming) {
      out_articles[found] = cand;
      if(out_distances) out_distances[found] = dist;
      found++;
      if(found >= max_out) break;
    }
  }
  return found;
}

size_t engine_formal_concepts(const OyezEngine *engine,
                              size_t min_support,
                              FormalConcept *out_concepts,
                              size_t max_concepts) {
  if(!engine || !out_concepts || max_concepts == 0 || engine->article_count == 0) return 0;
  if(min_support < 1u) min_support = 1u;

  size_t count = 0;
  /* Compute Galois closures for single & pair ontology attributes */
  for(uint32_t a = 1u; a < ONTOLOGY_NODE_COUNT && count < max_concepts; ++a) {
    const BitSet *ext_a = &engine->by_closure_ontology[a];
    uint32_t supp = bitset_popcount(ext_a);
    if(supp < min_support) continue;

    /* Galois prime operator A' -> A'': intersect masks of all objects in extent */
    uint32_t intent_onto = 0xFFFFFFFFu;
    uint32_t intent_reg = 0xFFFFFFFFu;
    uint32_t intent_nt = 0xFFFFFFFFu;
    FormalConcept fc;
    memset(&fc, 0, sizeof(fc));

    for(size_t w = 0; w < ext_a->word_cap; ++w) {
      uint64_t word = ext_a->words[w];
      while(word) {
        uint32_t bit = ctz64(word);
        uint32_t slot = (uint32_t)(w * 64u + bit);
        word &= (word - 1ULL);
        if(slot >= engine->article_count) continue;
        const ArticleRecord *art = &engine->articles[slot];
        intent_onto &= art->closure_ontology_mask;
        intent_reg &= (1u << (uint32_t)art->feed_region);
        intent_nt &= (1u << (uint32_t)art->news_type);
        if(fc.extent_count < 64u) fc.extent_slots[fc.extent_count++] = art->id;
      }
    }

    int dup = 0;
    for(size_t k = 0; k < count; ++k) {
      if(out_concepts[k].intent_ontology_mask == intent_onto &&
         out_concepts[k].extent_count == fc.extent_count) {
        dup = 1;
        break;
      }
    }
    if(dup) continue;

    fc.intent_ontology_mask = intent_onto;
    fc.intent_region_mask = intent_reg;
    fc.intent_news_type_mask = intent_nt;
    fc.stability_score = (double)supp * (double)popcnt64((uint64_t)intent_onto);
    out_concepts[count++] = fc;
  }
  return count;
}

size_t engine_categorical_pullback(const OyezEngine *engine,
                                   const char *left_region,
                                   const char *right_region,
                                   const char *pivot_dimension,
                                   PullbackPair *out_pairs,
                                   size_t max_pairs) {
  if(!engine || !left_region || !right_region || !out_pairs || max_pairs == 0) return 0;
  WorldRegion lr, rr;
  if(world_region_from_slug(left_region, &lr) != 0 ||
     world_region_from_slug(right_region, &rr) != 0) {
    return 0;
  }

  size_t found = 0;
  int by_country = (pivot_dimension && strcmp(pivot_dimension, "mentioned_country") == 0);
  int by_type = (pivot_dimension && strcmp(pivot_dimension, "news_type") == 0);

  for(size_t i = 0; i < engine->article_count && found < max_pairs; ++i) {
    const ArticleRecord *a = &engine->articles[i];
    if(a->feed_region != lr) continue;
    for(size_t j = 0; j < engine->article_count && found < max_pairs; ++j) {
      const ArticleRecord *b = &engine->articles[j];
      if(b->feed_region != rr || a->id == b->id) continue;

      const char *witness = NULL;
      if(by_country) {
        for(size_t ca = 0; ca < a->mentioned_country_count && !witness; ++ca) {
          for(size_t cb = 0; cb < b->mentioned_country_count; ++cb) {
            if(strcmp(a->mentioned_countries[ca], b->mentioned_countries[cb]) == 0) {
              witness = a->mentioned_countries[ca];
              break;
            }
          }
        }
      } else if(by_type) {
        if(a->news_type == b->news_type) {
          witness = news_type_slug(a->news_type);
        }
      } else {
        uint32_t shared = (a->direct_ontology_mask & b->direct_ontology_mask) & ~1u;
        if(shared) {
          uint32_t node = ctz64((uint64_t)shared);
          witness = ontology_node((OntologyNodeId)node)->slug;
        }
      }

      if(witness) {
        uint32_t dist = simhash64_distance(a->simhash64, b->simhash64);
        PullbackPair pair;
        pair.left = a;
        pair.right = b;
        pair.shared_witness = witness;
        pair.hamming_distance = dist;
        pair.coherence_score = 1.0 - ((double)dist / 64.0);
        out_pairs[found++] = pair;
      }
    }
  }
  return found;
}

size_t engine_bipartite_pagerank(const OyezEngine *engine,
                                 uint32_t iterations,
                                 double damping,
                                 RankedEntity *out_entities,
                                 size_t max_entities,
                                 ScoredArticle *out_articles,
                                 size_t max_articles) {
  if(!engine || engine->article_count == 0) return 0;

  /* Entities: 24 ontology nodes + up to 64 countries/orgs */
  RankedEntity ents[96];
  size_t ent_count = 0;

  for(uint32_t c = 1u; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t deg = bitset_popcount(&engine->by_closure_ontology[c]);
    if(deg == 0) continue;
    RankedEntity *e = &ents[ent_count++];
    snprintf(e->id, sizeof(e->id), "cat:%s", ontology_node((OntologyNodeId)c)->slug);
    snprintf(e->label, sizeof(e->label), "%s", ontology_node((OntologyNodeId)c)->slug);
    e->kind = "category";
    e->degree = deg;
    e->score = 1.0;
  }

  for(size_t i = 0; i < 128u && ent_count < 96u; ++i) {
    if(engine->by_mentioned_country[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_mentioned_country[i].bits);
      if(deg == 0) continue;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "geo:%s", engine->by_mentioned_country[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_mentioned_country[i].key);
      e->kind = "country";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  if(ent_count == 0) return 0;
  size_t N_art = engine->article_count;
  double *art_scores = malloc(N_art * sizeof(double));
  if(!art_scores) return 0;
  for(size_t i = 0; i < N_art; ++i) art_scores[i] = 1.0 / (double)N_art;
  for(size_t e = 0; e < ent_count; ++e) ents[e].score = 1.0 / (double)ent_count;

  if(iterations == 0) iterations = 12u;
  if(damping <= 0.0 || damping >= 1.0) damping = 0.85;

  for(uint32_t iter = 0; iter < iterations; ++iter) {
    for(size_t e = 0; e < ent_count; ++e) ents[e].score = (1.0 - damping) / (double)ent_count;

    for(size_t i = 0; i < N_art; ++i) {
      const ArticleRecord *a = &engine->articles[i];
      uint32_t out_deg = popcnt64((uint64_t)(a->closure_ontology_mask & ~1u)) + (uint32_t)a->mentioned_country_count;
      if(out_deg == 0) continue;
      double share = damping * (art_scores[i] / (double)out_deg);
      for(size_t e = 0; e < ent_count; ++e) {
        if(strcmp(ents[e].kind, "category") == 0) {
          OntologyNodeId nid;
          if(ontology_from_slug(ents[e].label, &nid) == 0 && (a->closure_ontology_mask & (1u << nid))) {
            ents[e].score += share;
          }
        } else {
          for(size_t c = 0; c < a->mentioned_country_count; ++c) {
            if(strcmp(a->mentioned_countries[c], ents[e].label) == 0) {
              ents[e].score += share;
              break;
            }
          }
        }
      }
    }

    for(size_t i = 0; i < N_art; ++i) {
      const ArticleRecord *a = &engine->articles[i];
      double sum = (1.0 - damping) / (double)N_art;
      for(size_t e = 0; e < ent_count; ++e) {
        if(ents[e].degree == 0) continue;
        int linked = 0;
        if(strcmp(ents[e].kind, "category") == 0) {
          OntologyNodeId nid;
          if(ontology_from_slug(ents[e].label, &nid) == 0 && (a->closure_ontology_mask & (1u << nid))) {
            linked = 1;
          }
        } else {
          for(size_t c = 0; c < a->mentioned_country_count; ++c) {
            if(strcmp(a->mentioned_countries[c], ents[e].label) == 0) {
              linked = 1;
              break;
            }
          }
        }
        if(linked) {
          sum += damping * (ents[e].score / (double)ents[e].degree);
        }
      }
      art_scores[i] = sum;
    }
  }

  /* Sort entities descending */
  for(size_t i = 0; i < ent_count; ++i) {
    for(size_t j = i + 1u; j < ent_count; ++j) {
      if(ents[j].score > ents[i].score) {
        RankedEntity tmp = ents[i];
        ents[i] = ents[j];
        ents[j] = tmp;
      }
    }
  }

  size_t out_e = ent_count < max_entities ? ent_count : max_entities;
  if(out_entities && out_e > 0) {
    memcpy(out_entities, ents, out_e * sizeof(RankedEntity));
  }

  if(out_articles && max_articles > 0) {
    ScoredArticle *all_a = malloc(N_art * sizeof(ScoredArticle));
    if(all_a) {
      for(size_t i = 0; i < N_art; ++i) {
        all_a[i].article = &engine->articles[i];
        all_a[i].score = art_scores[i];
      }
      qsort(all_a, N_art, sizeof(ScoredArticle), compare_scored_desc);
      size_t take = N_art < max_articles ? N_art : max_articles;
      memcpy(out_articles, all_a, take * sizeof(ScoredArticle));
      free(all_a);
    }
  }

  free(art_scores);
  return out_e;
}

static void wire_append_cstr(Buffer *buf, const char *s) {
  if(!s) s = "";
  uint32_t len = (uint32_t)strlen(s);
  wire_append_u32(buf, len);
  buffer_append(buf, s, len);
}

static char *wire_read_cstr(const unsigned char *bytes, size_t total_len, size_t *offset) {
  uint32_t len = 0;
  if(!wire_read_u32(bytes, total_len, offset, &len)) return NULL;
  if(len > total_len - *offset) return NULL;
  char *out = malloc((size_t)len + 1u);
  if(!out) return NULL;
  if(len > 0) memcpy(out, bytes + *offset, len);
  out[len] = '\0';
  *offset += len;
  return out;
}

int engine_save_snapshot(const OyezEngine *engine, const char *path) {
  if(!engine || !path) return -1;
  Buffer buf;
  buffer_init(&buf);
  buffer_append(&buf, "OYEZIX01", 8u);
  wire_append_u32(&buf, (uint32_t)engine->feed_count);
  wire_append_u32(&buf, (uint32_t)engine->article_count);

  for(size_t i = 0; i < engine->feed_count; ++i) {
    const FeedRecord *f = &engine->feeds[i];
    wire_append_u32(&buf, f->id);
    wire_append_cstr(&buf, f->title);
    wire_append_cstr(&buf, f->feed_url);
    wire_append_cstr(&buf, f->site_url);
    wire_append_cstr(&buf, f->category);
    wire_append_u32(&buf, (uint32_t)f->region);
    wire_append_cstr(&buf, f->country);
    wire_append_cstr(&buf, f->subregion);
    wire_append_cstr(&buf, f->language);
    wire_append_cstr(&buf, f->source_tier);
    wire_append_cstr(&buf, f->editorial_focus);
  }

  for(size_t i = 0; i < engine->article_count; ++i) {
    const ArticleRecord *a = &engine->articles[i];
    wire_append_u32(&buf, a->feed_id);
    wire_append_cstr(&buf, a->guid);
    wire_append_cstr(&buf, a->title);
    wire_append_cstr(&buf, a->url);
    wire_append_cstr(&buf, a->author);
    wire_append_cstr(&buf, a->summary);
    wire_append_cstr(&buf, a->content_html);
    wire_append_cstr(&buf, a->published_at);
    wire_append_u64(&buf, a->published_ms);
    wire_append_cstr(&buf, a->image_url);
  }

  uint32_t crc = checksum_crc32(buf.data, buf.len);
  wire_append_u32(&buf, crc);

  FILE *fp = fopen(path, "wb");
  if(!fp) {
    buffer_free(&buf);
    return -1;
  }
  size_t written = fwrite(buf.data, 1u, buf.len, fp);
  fclose(fp);
  size_t expected = buf.len;
  buffer_free(&buf);
  return written == expected ? 0 : -1;
}

int engine_load_snapshot(OyezEngine *engine, const char *path) {
  if(!engine || !path) return -1;
  FILE *fp = fopen(path, "rb");
  if(!fp) return -1;
  if(fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long sz = ftell(fp);
  if(sz < 20 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }
  unsigned char *bytes = malloc((size_t)sz);
  if(!bytes) {
    fclose(fp);
    return -1;
  }
  if(fread(bytes, 1u, (size_t)sz, fp) != (size_t)sz) {
    free(bytes);
    fclose(fp);
    return -1;
  }
  fclose(fp);

  size_t payload_len = (size_t)sz - 4u;
  size_t crc_off = payload_len;
  uint32_t stored_crc = 0;
  wire_read_u32(bytes, (size_t)sz, &crc_off, &stored_crc);
  if(stored_crc != checksum_crc32(bytes, payload_len) || memcmp(bytes, "OYEZIX01", 8u) != 0) {
    free(bytes);
    return -1;
  }

  engine_free(engine);
  engine_init(engine);

  size_t off = 8u;
  uint32_t fcount = 0;
  uint32_t acount = 0;
  if(!wire_read_u32(bytes, payload_len, &off, &fcount) ||
     !wire_read_u32(bytes, payload_len, &off, &acount)) {
    goto fail;
  }

  for(uint32_t i = 0; i < fcount; ++i) {
    uint32_t fid = 0;
    if(!wire_read_u32(bytes, payload_len, &off, &fid)) goto fail;
    char *title = wire_read_cstr(bytes, payload_len, &off);
    char *furl = wire_read_cstr(bytes, payload_len, &off);
    char *surl = wire_read_cstr(bytes, payload_len, &off);
    char *cat = wire_read_cstr(bytes, payload_len, &off);
    uint32_t reg = 0;
    if(!wire_read_u32(bytes, payload_len, &off, &reg)) {
      free(title); free(furl); free(surl); free(cat);
      goto fail;
    }
    char *country = wire_read_cstr(bytes, payload_len, &off);
    char *subreg = wire_read_cstr(bytes, payload_len, &off);
    char *lang = wire_read_cstr(bytes, payload_len, &off);
    char *tier = wire_read_cstr(bytes, payload_len, &off);
    char *focus = wire_read_cstr(bytes, payload_len, &off);
    if(!title || !furl || !surl || !cat || !country || !subreg || !lang || !tier || !focus) {
      free(title); free(furl); free(surl); free(cat);
      free(country); free(subreg); free(lang); free(tier); free(focus);
      goto fail;
    }
    CatalogEntry ce = {title, furl, surl, cat, (WorldRegion)reg, country, subreg, lang, tier, focus};
    uint32_t actual_id = 0;
    engine_upsert_feed(engine, &ce, &actual_id);
    if(engine->feed_count > 0) {
      engine->feeds[engine->feed_count - 1u].id = fid;
      if(fid >= engine->next_feed_id) engine->next_feed_id = fid + 1u;
    }
    free(title); free(furl); free(surl); free(cat);
    free(country); free(subreg); free(lang); free(tier); free(focus);
  }

  for(uint32_t i = 0; i < acount; ++i) {
    uint32_t fid = 0;
    if(!wire_read_u32(bytes, payload_len, &off, &fid)) goto fail;
    char *guid = wire_read_cstr(bytes, payload_len, &off);
    char *title = wire_read_cstr(bytes, payload_len, &off);
    char *url = wire_read_cstr(bytes, payload_len, &off);
    char *author = wire_read_cstr(bytes, payload_len, &off);
    char *summary = wire_read_cstr(bytes, payload_len, &off);
    char *chtml = wire_read_cstr(bytes, payload_len, &off);
    char *pub_at = wire_read_cstr(bytes, payload_len, &off);
    uint64_t pub_ms = 0;
    if(!wire_read_u64(bytes, payload_len, &off, &pub_ms)) {
      free(guid); free(title); free(url); free(author); free(summary); free(chtml); free(pub_at);
      goto fail;
    }
    char *img = wire_read_cstr(bytes, payload_len, &off);
    if(!guid || !title || !url || !author || !summary || !chtml || !pub_at || !img) {
      free(guid); free(title); free(url); free(author); free(summary); free(chtml); free(pub_at); free(img);
      goto fail;
    }
    engine_ingest_article(engine, fid, guid, title, url, author, summary, chtml, pub_at, pub_ms, img, NULL, 0, NULL, NULL);
    free(guid); free(title); free(url); free(author); free(summary); free(chtml); free(pub_at); free(img);
  }

  free(bytes);
  return 0;

fail:
  free(bytes);
  return -1;
}
