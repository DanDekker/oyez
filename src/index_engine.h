#ifndef OYEZ_INDEX_ENGINE_H
#define OYEZ_INDEX_ENGINE_H

#include "catalog.h"
#include "core.h"
#include "ontology.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t *words;
  size_t word_cap;
  uint32_t max_slot;
} BitSet;

typedef struct {
  uint32_t id;
  char *title;
  char *feed_url;
  char *site_url;
  char *description;
  char *category;
  WorldRegion region;
  char country[4];
  char *subregion;
  char *language;
  char *source_tier;
  char *editorial_focus;
  uint64_t last_fetched_ms;
  char *last_error;
  uint32_t fetch_count;
  uint32_t error_count;
  uint32_t article_count;
} FeedRecord;

typedef struct {
  uint32_t id;
  uint32_t slot;
  uint32_t feed_id;
  char *guid;
  char *title;
  char *url;
  char *author;
  char *summary;
  char *content_text;
  char *content_html;
  char *published_at;
  uint64_t published_ms;
  char *image_url;
  char *feed_title;
  char *feed_category;
  WorldRegion feed_region;
  char feed_country[4];
  char *feed_language;
  char *feed_source_tier;
  NewsType news_type;
  uint32_t direct_ontology_mask;
  uint32_t closure_ontology_mask;
  char mentioned_countries[MAX_EXTRACTED_COUNTRIES][4];
  size_t mentioned_country_count;
  char mentioned_orgs[MAX_EXTRACTED_ORGS][24];
  size_t mentioned_org_count;
  char mentioned_cves[MAX_EXTRACTED_CVES][20];
  size_t mentioned_cve_count;
  char mentioned_assets[MAX_EXTRACTED_ASSETS][16];
  size_t mentioned_asset_count;
  char mentioned_fiscal_events[MAX_EXTRACTED_EVENTS][24];
  size_t mentioned_fiscal_event_count;
  ClearanceLevel clearance;
  double opsec_score;
  FiscalStance fiscal_stance;
  double macro_score;
  uint32_t word_count;
  uint32_t reading_time_minutes;
  uint64_t simhash64;
  uint32_t doc_len;
} ArticleRecord;

typedef struct {
  char term[32];
  uint64_t hash;
  uint32_t *slots;
  float *weights;
  uint32_t count;
  uint32_t cap;
  int occupied;
} InvertedBucket;

typedef struct {
  uint16_t band_key;
  uint32_t *slots;
  uint32_t count;
  uint32_t cap;
} SimHashBucket;

typedef struct {
  uint32_t cluster_id;
  const ArticleRecord *lead_article;
  const ArticleRecord *members[16];
  size_t member_count;
  uint32_t region_mask;
  uint32_t ontology_mask;
  double cluster_score;
} StoryCluster;

typedef struct {
  char key[24];
  BitSet bits;
  int occupied;
} CountryFiber;

typedef struct {
  FeedRecord *feeds;
  size_t feed_count;
  size_t feed_cap;
  uint32_t next_feed_id;

  ArticleRecord *articles;
  size_t article_count;
  size_t article_cap;
  uint32_t next_article_id;
  uint64_t total_doc_len;

  BitSet all_articles;
  BitSet by_region[WORLD_REGION_COUNT];
  BitSet by_news_type[NEWS_TYPE_COUNT];
  BitSet by_clearance[CLEARANCE_LEVEL_COUNT];
  BitSet by_fiscal_stance[FISCAL_STANCE_COUNT];
  BitSet by_direct_ontology[ONTOLOGY_NODE_COUNT];
  BitSet by_closure_ontology[ONTOLOGY_NODE_COUNT];

  CountryFiber by_source_country[256];
  CountryFiber by_mentioned_country[256];
  CountryFiber by_asset[128];
  CountryFiber by_fiscal_event[64];
  CountryFiber by_cve[128];
  CountryFiber by_org[128];

  InvertedBucket *inv_table;
  size_t inv_cap;
  size_t inv_count;

  /* 4-band x 16-bit Multi-Probe SimHash LSH index (4 * 4096 hash buckets) */
  SimHashBucket *lsh_bands[4];

  /* 64-entry FNV-1a Open-Addressed Query Response Cache */
  struct {
    uint64_t key_hash;
    uint32_t *slots;
    float *scores;
    size_t count;
    size_t total;
    int valid;
  } query_cache[64];
  uint64_t index_generation;
} OyezEngine;

typedef struct {
  const char *query;
  const char *category;
  int use_category_morphism;
  const char *news_type;
  const char *clearance;
  const char *fiscal_stance;
  const char *asset;
  const char *fiscal_event;
  const char *cve;
  const char *organization;
  const char *region;
  const char *country;
  const char *mentioned_country;
  const char *language;
  const char *source_tier;
  uint32_t feed_id;
  int diversify_mmr;
  double mmr_lambda;
  size_t limit;
  size_t offset;
} ArticleQuery;

typedef struct {
  const ArticleRecord *article;
  double score;
} ScoredArticle;

typedef struct {
  ScoredArticle *hits;
  size_t count;
  size_t total;
  double query_time_ms;
} SearchResponse;

typedef struct {
  uint32_t extent_slots[64];
  size_t extent_count;
  uint32_t intent_ontology_mask;
  uint32_t intent_region_mask;
  uint32_t intent_news_type_mask;
  double stability_score;
} FormalConcept;

typedef struct {
  const ArticleRecord *left;
  const ArticleRecord *right;
  const char *shared_witness;
  uint32_t hamming_distance;
  double coherence_score;
} PullbackPair;

typedef struct {
  char id[32];
  char label[32];
  const char *kind;
  double score;
  uint32_t degree;
} RankedEntity;

void bitset_init(BitSet *bs);
void bitset_free(BitSet *bs);
int bitset_set(BitSet *bs, uint32_t bit);
int bitset_test(const BitSet *bs, uint32_t bit);
uint32_t bitset_popcount(const BitSet *bs);
int bitset_clone(const BitSet *src, BitSet *dst);
void bitset_and_inplace(BitSet *dst, const BitSet *other);
int bitset_or_inplace(BitSet *dst, const BitSet *other);

void engine_init(OyezEngine *engine);
void engine_free(OyezEngine *engine);

int engine_upsert_feed(OyezEngine *engine, const CatalogEntry *entry, uint32_t *out_feed_id);
const FeedRecord *engine_find_feed(const OyezEngine *engine, uint32_t feed_id);
const FeedRecord *engine_find_feed_by_url(const OyezEngine *engine, const char *feed_url);
size_t engine_seed_catalog(OyezEngine *engine, const char *region_slug, const char *country_code);

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
                          int *out_inserted);

const ArticleRecord *engine_get_article_by_id(const OyezEngine *engine, uint32_t id);
int engine_search(const OyezEngine *engine, const ArticleQuery *q, SearchResponse *out);
void search_response_free(SearchResponse *res);

size_t engine_find_near_duplicates(const OyezEngine *engine,
                                   uint32_t article_id,
                                   uint32_t max_hamming,
                                   const ArticleRecord **out_articles,
                                   uint32_t *out_distances,
                                   size_t max_out);

size_t engine_formal_concepts(const OyezEngine *engine,
                              size_t min_support,
                              FormalConcept *out_concepts,
                              size_t max_concepts);

size_t engine_categorical_pullback(const OyezEngine *engine,
                                   const char *left_region,
                                   const char *right_region,
                                   const char *pivot_dimension,
                                   PullbackPair *out_pairs,
                                   size_t max_pairs);

size_t engine_bipartite_pagerank(const OyezEngine *engine,
                                 uint32_t iterations,
                                 double damping,
                                 RankedEntity *out_entities,
                                 size_t max_entities,
                                 ScoredArticle *out_articles,
                                 size_t max_articles);

size_t engine_story_clusters(const OyezEngine *engine,
                             uint32_t max_hamming,
                             StoryCluster *out_clusters,
                             size_t max_clusters);

int engine_save_snapshot(const OyezEngine *engine, const char *path);
int engine_load_snapshot(OyezEngine *engine, const char *path);

#endif
