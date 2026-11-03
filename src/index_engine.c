#include "index_engine.h"

#include "checksum.h"
#include "m1_tokenizer.h"
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

/* ============================================================================
 * Contiguous 64 KB Block String Arena (O(1) Bump Allocator for Article Strings)
 * ============================================================================
 */
#define ARENA_BLOCK_DEFAULT_CAP 65536u

struct StringArenaBlock {
  struct StringArenaBlock *next;
  size_t used;
  size_t cap;
  char data[];
};

static char *arena_strdup(OyezEngine *engine, const char *s) {
  if(!s) s = "";
  size_t need = strlen(s) + 1u;
  struct StringArenaBlock *blk = engine->arena_head;
  if(!blk || blk->cap - blk->used < need) {
    size_t bcap = need > (ARENA_BLOCK_DEFAULT_CAP / 2u) ? (need * 2u) : ARENA_BLOCK_DEFAULT_CAP;
    blk = malloc(sizeof(struct StringArenaBlock) + bcap);
    if(!blk) return (char *)"";
    blk->next = engine->arena_head;
    blk->used = 0;
    blk->cap = bcap;
    engine->arena_head = blk;
  }
  char *dst = blk->data + blk->used;
  memcpy(dst, s, need);
  blk->used += need;
  return dst;
}

static void arena_free_all(OyezEngine *engine) {
  struct StringArenaBlock *blk = engine->arena_head;
  while(blk) {
    struct StringArenaBlock *next = blk->next;
    free(blk);
    blk = next;
  }
  engine->arena_head = NULL;
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

void bitset_andnot_inplace(BitSet *dst, const BitSet *other) {
  if(!dst || !dst->words || !other || !other->words) return;
  size_t min_w = dst->word_cap < other->word_cap ? dst->word_cap : other->word_cap;
  for(size_t i = 0; i < min_w; ++i) {
    dst->words[i] &= ~other->words[i];
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

uint32_t bitset_intersect_count(const BitSet *a, const BitSet *b) {
  if(!a || !b || !a->words || !b->words) return 0u;
  size_t min_w = a->word_cap < b->word_cap ? a->word_cap : b->word_cap;
  uint32_t total = 0u;
  for(size_t i = 0; i < min_w; ++i) {
    total += popcnt64(a->words[i] & b->words[i]);
  }
  return total;
}

double bitset_jaccard(const BitSet *a, const BitSet *b) {
  if(!a || !b || !a->words || !b->words) return 0.0;
  size_t max_w = a->word_cap > b->word_cap ? a->word_cap : b->word_cap;
  uint32_t inter = 0u;
  uint32_t uni = 0u;
  for(size_t i = 0; i < max_w; ++i) {
    uint64_t wa = (i < a->word_cap) ? a->words[i] : 0ULL;
    uint64_t wb = (i < b->word_cap) ? b->words[i] : 0ULL;
    inter += popcnt64(wa & wb);
    uni += popcnt64(wa | wb);
  }
  return uni > 0u ? ((double)inter / (double)uni) : 0.0;
}

static int dedup_table_grow(OyezEngine *engine) {
  size_t next_cap = engine->dedup_cap ? (engine->dedup_cap * 2u) : 1024u;
  void *raw = calloc(next_cap, sizeof(*engine->dedup_table));
  if(!raw) return -1;
  typeof(engine->dedup_table) next = raw;
  size_t mask = next_cap - 1u;
  for(size_t i = 0; i < engine->dedup_cap; ++i) {
    if(engine->dedup_table[i].occupied) {
      size_t idx = (size_t)engine->dedup_table[i].key_hash & mask;
      while(next[idx].occupied) idx = (idx + 1u) & mask;
      next[idx] = engine->dedup_table[i];
    }
  }
  free(engine->dedup_table);
  engine->dedup_table = next;
  engine->dedup_cap = next_cap;
  return 0;
}

static uint32_t dedup_lookup(const OyezEngine *engine, uint64_t key_hash) {
  if(!engine->dedup_table || engine->dedup_cap == 0) return 0u;
  size_t mask = engine->dedup_cap - 1u;
  size_t idx = (size_t)key_hash & mask;
  for(size_t step = 0; step < engine->dedup_cap; ++step) {
    size_t pos = (idx + step) & mask;
    if(!engine->dedup_table[pos].occupied) return 0u;
    if(engine->dedup_table[pos].key_hash == key_hash) {
      return engine->dedup_table[pos].article_id;
    }
  }
  return 0u;
}

static void dedup_insert(OyezEngine *engine, uint64_t key_hash, uint32_t article_id) {
  if((engine->dedup_count + 2u) * 10u >= engine->dedup_cap * 7u) {
    if(dedup_table_grow(engine) != 0) return;
  }
  size_t mask = engine->dedup_cap - 1u;
  size_t idx = (size_t)key_hash & mask;
  while(engine->dedup_table[idx].occupied) {
    if(engine->dedup_table[idx].key_hash == key_hash) return;
    idx = (idx + 1u) & mask;
  }
  engine->dedup_table[idx].key_hash = key_hash;
  engine->dedup_table[idx].article_id = article_id;
  engine->dedup_table[idx].occupied = 1;
  engine->dedup_count++;
}

static BitSet *country_fiber_get(CountryFiber *table, size_t cap, const char *code, int create) {
  if(!code || code[0] == '\0') return NULL;
  char norm[64];
  size_t k = 0;
  while(code[k] && k < 63u) {
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

static int inv_post(OyezEngine *engine, const char *term, uint64_t hash, uint32_t slot, float weight, uint32_t pos_mask) {
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
        uint32_t *npm = realloc(b->pos_masks, ncap * sizeof(uint32_t));
        if(!nslots || !nw || !npm) {
          if(nslots) b->slots = nslots;
          if(nw) b->weights = nw;
          if(npm) b->pos_masks = npm;
          return -1;
        }
        b->slots = nslots;
        b->weights = nw;
        b->pos_masks = npm;
        b->cap = ncap;
      }
      b->slots[b->count] = slot;
      b->weights[b->count] = weight;
      b->pos_masks[b->count] = pos_mask;
      b->count++;
      return 0;
    }
    idx = (idx + 1u) & mask;
  }

  InvertedBucket *b = &engine->inv_table[idx];
  uint32_t *slots = malloc(8u * sizeof(uint32_t));
  float *weights = malloc(8u * sizeof(float));
  uint32_t *pos_masks = malloc(8u * sizeof(uint32_t));
  if(!slots || !weights || !pos_masks) {
    free(slots);
    free(weights);
    free(pos_masks);
    return -1;
  }
  b->occupied = 1;
  strncpy(b->term, term, 31u);
  b->term[31] = '\0';
  b->hash = hash;
  b->cap = 8u;
  b->slots = slots;
  b->weights = weights;
  b->pos_masks = pos_masks;
  b->slots[0] = slot;
  b->weights[0] = weight;
  b->pos_masks[0] = pos_mask;
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

static const char *const k_auto_action_slugs[4] = {
    "alert-ring",
    "jsonl-log",
    "webhook-post",
    "snapshot-checkpoint",
};

const char *automation_action_slug(AutomationActionType act) {
  if((unsigned)act >= 4u) return "alert-ring";
  return k_auto_action_slugs[act];
}

int automation_action_from_slug(const char *slug, AutomationActionType *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < 4u; ++i) {
    if(strcmp(slug, k_auto_action_slugs[i]) == 0) {
      *out = (AutomationActionType)i;
      return 0;
    }
  }
  return -1;
}

static int equals_icase(const char *a, const char *b) {
  if(!a || !b) return 0;
  while(*a && *b) {
    if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static int contains_icase(const char *haystack, const char *needle) {
  if(!haystack || !needle || !needle[0]) return 0;
  size_t nlen = strlen(needle);
  for(const char *p = haystack; *p; ++p) {
    size_t k = 0;
    while(k < nlen && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)needle[k])) {
      k++;
    }
    if(k == nlen) return 1;
  }
  return 0;
}

static int article_matches_automation_rule(const ArticleRecord *a, const AutomationRule *r, double *out_score) {
  if(!a || !r || !r->enabled) return 0;
  if(a->clearance < r->min_clearance) return 0;
  if(a->ai_tier < r->min_ai_tier) return 0;
  if(a->weather_severity < r->min_weather_severity) return 0;
  if(r->fiscal_stance != FISCAL_NEUTRAL && a->fiscal_stance != r->fiscal_stance) return 0;

  if(r->category[0] != '\0') {
    OntologyNodeId node = ONTO_ROOT;
    if(ontology_from_slug(r->category, &node) == 0 && node != ONTO_ROOT) {
      if((a->closure_ontology_mask & (1u << node)) == 0u) return 0;
    }
  }
  if(r->region[0] != '\0') {
    WorldRegion reg = REGION_GLOBAL;
    if(world_region_from_slug(r->region, &reg) == 0 && reg != REGION_GLOBAL) {
      if(a->feed_region != reg) return 0;
    }
  }
  if(r->weather[0] != '\0') {
    int found_wx = 0;
    for(size_t i = 0; i < a->mentioned_weather_count; ++i) {
      if(equals_icase(a->mentioned_weather[i], r->weather)) {
        found_wx = 1;
        break;
      }
    }
    if(!found_wx) return 0;
  }
  if(r->query[0] != '\0') {
    int found_q = 0;
    if(a->title && contains_icase(a->title, r->query)) found_q = 1;
    else if(a->summary && contains_icase(a->summary, r->query)) found_q = 1;
    if(!found_q) return 0;
  }

  double composite = a->opsec_score + a->macro_score + a->ai_compute_score + a->weather_score + a->sports_score;
  if(r->min_score > 0.0 && composite < r->min_score) return 0;
  if(out_score) *out_score = composite > 0.0 ? composite : 1.0;
  return 1;
}

static size_t evaluate_article_automation_rules(OyezEngine *engine, const ArticleRecord *a) {
  if(!engine || !a) return 0;
  size_t fired = 0;
  for(size_t i = 0; i < engine->automation_rule_count; ++i) {
    AutomationRule *r = &engine->automation_rules[i];
    double trig_score = 0.0;
    if(!article_matches_automation_rule(a, r, &trig_score)) continue;

    /* Avoid duplicate alert for the same (rule_id, article_id) in ring buffer */
    int already = 0;
    for(size_t k = 0; k < engine->automation_alert_count; ++k) {
      if(engine->automation_alerts[k].rule_id == r->id &&
         engine->automation_alerts[k].article_id == a->id) {
        already = 1;
        break;
      }
    }
    if(already) continue;

    r->hit_count++;
    r->last_triggered_ms = a->published_ms > 0 ? a->published_ms : wall_time_ms();

    size_t slot = engine->automation_alert_head % MAX_AUTOMATION_ALERTS;
    engine->automation_alert_head = (engine->automation_alert_head + 1u) % MAX_AUTOMATION_ALERTS;
    if(engine->automation_alert_count < MAX_AUTOMATION_ALERTS) {
      engine->automation_alert_count++;
    }

    AutomationAlert *al = &engine->automation_alerts[slot];
    memset(al, 0, sizeof(*al));
    al->alert_id = engine->next_alert_id++;
    al->rule_id = r->id;
    strncpy(al->rule_name, r->name, sizeof(al->rule_name) - 1u);
    al->article_id = a->id;
    strncpy(al->article_title, a->title ? a->title : "", sizeof(al->article_title) - 1u);
    strncpy(al->article_url, a->url ? a->url : "", sizeof(al->article_url) - 1u);
    strncpy(al->category_slug, a->feed_category ? a->feed_category : "all-news", sizeof(al->category_slug) - 1u);
    strncpy(al->weather_severity_slug, weather_severity_slug(a->weather_severity), sizeof(al->weather_severity_slug) - 1u);
    strncpy(al->clearance_slug, clearance_level_slug(a->clearance), sizeof(al->clearance_slug) - 1u);
    al->trigger_score = trig_score;
    al->triggered_at_ms = r->last_triggered_ms;
    al->action_taken = r->action;

    if(r->action == AUTO_ACTION_JSONL_LOG) {
      const char *log_path = (r->target[0] != '\0') ? r->target : "data/alerts.jsonl";
      /* Only write to safe relative paths inside data/ */
      if(strncmp(log_path, "data/", 5u) == 0 && strstr(log_path, "..") == NULL) {
        FILE *fp = fopen(log_path, "ab");
        if(fp) {
          fprintf(fp,
                  "{\"alert_id\":%llu,\"rule\":\"%s\",\"article_id\":%u,\"weather_severity\":\"%s\",\"score\":%.2f}\n",
                  (unsigned long long)al->alert_id,
                  al->rule_name,
                  al->article_id,
                  al->weather_severity_slug,
                  al->trigger_score);
          fclose(fp);
        }
      }
    } else if(r->action == AUTO_ACTION_SNAPSHOT) {
      const char *snap_path = (r->target[0] != '\0') ? r->target : "data/oyez_auto.ix";
      if(strncmp(snap_path, "data/", 5u) == 0 && strstr(snap_path, "..") == NULL &&
         strstr(snap_path, ".ix") != NULL) {
        (void)engine_save_snapshot(engine, snap_path);
      }
    }
    fired++;
  }
  return fired;
}

static void seed_default_automation_rules(OyezEngine *engine) {
  engine->automation_rule_count = 0;
  engine->next_rule_id = 1u;
  engine->next_alert_id = 1u;

  AutomationRule r1 = {0};
  r1.id = engine->next_rule_id++;
  strcpy(r1.name, "severe-weather-hazard");
  r1.min_weather_severity = WEATHER_WARNING;
  r1.action = AUTO_ACTION_ALERT_RING;
  r1.enabled = 1;
  engine->automation_rules[engine->automation_rule_count++] = r1;

  AutomationRule r2 = {0};
  r2.id = engine->next_rule_id++;
  strcpy(r2.name, "zero-day-cyber-alert");
  strcpy(r2.category, "cyber");
  r2.min_clearance = CLEARANCE_ZERO_DAY;
  r2.action = AUTO_ACTION_ALERT_RING;
  r2.enabled = 1;
  engine->automation_rules[engine->automation_rule_count++] = r2;

  AutomationRule r3 = {0};
  r3.id = engine->next_rule_id++;
  strcpy(r3.name, "frontier-ai-release");
  strcpy(r3.category, "ai");
  r3.min_ai_tier = AI_TIER_FRONTIER_PRETRAINING;
  r3.action = AUTO_ACTION_ALERT_RING;
  r3.enabled = 1;
  engine->automation_rules[engine->automation_rule_count++] = r3;

  AutomationRule r4 = {0};
  r4.id = engine->next_rule_id++;
  strcpy(r4.name, "macro-central-bank-shock");
  strcpy(r4.category, "finance");
  r4.min_score = 4.5;
  r4.action = AUTO_ACTION_ALERT_RING;
  r4.enabled = 1;
  engine->automation_rules[engine->automation_rule_count++] = r4;

  AutomationRule r5 = {0};
  r5.id = engine->next_rule_id++;
  strcpy(r5.name, "catastrophic-planetary-emergency");
  r5.min_weather_severity = WEATHER_CATASTROPHIC;
  r5.action = AUTO_ACTION_JSONL_LOG;
  strcpy(r5.target, "data/automation_alerts.jsonl");
  r5.enabled = 1;
  engine->automation_rules[engine->automation_rule_count++] = r5;
}

void engine_init(OyezEngine *engine) {
  ontology_init();
  memset(engine, 0, sizeof(*engine));
  pthread_rwlock_init(&engine->rwlock, NULL);
  pthread_mutex_init(&engine->cache_lock, NULL);
  engine->next_feed_id = 1u;
  engine->next_article_id = 1u;
  seed_default_automation_rules(engine);
  inv_table_grow(engine);
  dedup_table_grow(engine);
  for(int b = 0; b < 4; ++b) {
    engine->lsh_bands[b] = calloc(4096u, sizeof(SimHashBucket));
  }
}

void engine_read_lock(const OyezEngine *engine) {
  if(!engine) return;
  pthread_rwlock_rdlock((pthread_rwlock_t *)&engine->rwlock);
}

void engine_write_lock(OyezEngine *engine) {
  if(!engine) return;
  pthread_rwlock_wrlock(&engine->rwlock);
}

void engine_unlock(const OyezEngine *engine) {
  if(!engine) return;
  pthread_rwlock_unlock((pthread_rwlock_t *)&engine->rwlock);
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
  /* Strings allocated via engine->arena_head are released in bulk by arena_free_all */
  free(a->content_text);
}

void engine_free(OyezEngine *engine) {
  if(!engine) return;
  for(size_t i = 0; i < 64u; ++i) {
    if(engine->query_cache[i].valid) {
      free(engine->query_cache[i].slots);
      free(engine->query_cache[i].scores);
    }
  }
  for(size_t i = 0; i < engine->feed_count; ++i) {
    free_feed_record(&engine->feeds[i]);
  }
  free(engine->feeds);

  for(size_t i = 0; i < engine->article_count; ++i) {
    free_article_record(&engine->articles[i]);
  }
  free(engine->articles);
  arena_free_all(engine);

  bitset_free(&engine->all_articles);
  for(size_t i = 0; i < WORLD_REGION_COUNT; ++i) bitset_free(&engine->by_region[i]);
  for(size_t i = 0; i < NEWS_TYPE_COUNT; ++i) bitset_free(&engine->by_news_type[i]);
  for(size_t i = 0; i < CLEARANCE_LEVEL_COUNT; ++i) bitset_free(&engine->by_clearance[i]);
  for(size_t i = 0; i < FISCAL_STANCE_COUNT; ++i) bitset_free(&engine->by_fiscal_stance[i]);
  for(size_t i = 0; i < AI_TIER_COUNT; ++i) bitset_free(&engine->by_ai_tier[i]);
  for(size_t i = 0; i < AI_LICENSE_COUNT; ++i) bitset_free(&engine->by_ai_license[i]);
  for(size_t i = 0; i < WEATHER_SEVERITY_COUNT; ++i) bitset_free(&engine->by_weather_severity[i]);
  for(size_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    bitset_free(&engine->by_direct_ontology[i]);
    bitset_free(&engine->by_closure_ontology[i]);
  }
  for(size_t i = 0; i < 256u; ++i) {
    if(engine->by_source_country[i].occupied) bitset_free(&engine->by_source_country[i].bits);
    if(engine->by_mentioned_country[i].occupied) bitset_free(&engine->by_mentioned_country[i].bits);
  }
  for(size_t i = 0; i < 128u; ++i) {
    if(engine->by_asset[i].occupied) bitset_free(&engine->by_asset[i].bits);
    if(engine->by_cve[i].occupied) bitset_free(&engine->by_cve[i].bits);
    if(engine->by_org[i].occupied) bitset_free(&engine->by_org[i].bits);
    if(engine->by_ai_model[i].occupied) bitset_free(&engine->by_ai_model[i].bits);
    if(engine->by_ai_infra[i].occupied) bitset_free(&engine->by_ai_infra[i].bits);
    if(engine->by_hf_repo[i].occupied) bitset_free(&engine->by_hf_repo[i].bits);
    if(engine->by_sport[i].occupied) bitset_free(&engine->by_sport[i].bits);
    if(engine->by_weather[i].occupied) bitset_free(&engine->by_weather[i].bits);
  }
  for(size_t i = 0; i < 64u; ++i) {
    if(engine->by_fiscal_event[i].occupied) bitset_free(&engine->by_fiscal_event[i].bits);
  }
  for(size_t i = 0; i < engine->inv_cap; ++i) {
    if(engine->inv_table[i].occupied) {
      free(engine->inv_table[i].slots);
      free(engine->inv_table[i].weights);
      free(engine->inv_table[i].pos_masks);
    }
  }
  free(engine->inv_table);
  free(engine->dedup_table);
  for(int b = 0; b < 4; ++b) {
    if(engine->lsh_bands[b]) {
      for(size_t k = 0; k < 4096u; ++k) {
        free(engine->lsh_bands[b][k].slots);
      }
      free(engine->lsh_bands[b]);
    }
  }
  pthread_mutex_destroy(&engine->cache_lock);
  pthread_rwlock_destroy(&engine->rwlock);
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
  if(!engine || feed_id == 0) return NULL;
  if((size_t)(feed_id - 1u) < engine->feed_count && engine->feeds[feed_id - 1u].id == feed_id) {
    return &engine->feeds[feed_id - 1u];
  }
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
  if((unsigned)a->clearance < CLEARANCE_LEVEL_COUNT) {
    bitset_set(&engine->by_clearance[a->clearance], slot);
  }
  if((unsigned)a->fiscal_stance < FISCAL_STANCE_COUNT) {
    bitset_set(&engine->by_fiscal_stance[a->fiscal_stance], slot);
  }
  if((unsigned)a->ai_tier < AI_TIER_COUNT) {
    bitset_set(&engine->by_ai_tier[a->ai_tier], slot);
  }
  if((unsigned)a->ai_license < AI_LICENSE_COUNT) {
    bitset_set(&engine->by_ai_license[a->ai_license], slot);
  }
  if((unsigned)a->weather_severity < WEATHER_SEVERITY_COUNT) {
    bitset_set(&engine->by_weather_severity[a->weather_severity], slot);
  }
  for(uint32_t node = 0; node < ONTOLOGY_NODE_COUNT; ++node) {
    if(a->direct_ontology_mask & (1u << node)) {
      bitset_set(&engine->by_direct_ontology[node], slot);
    }
    if(a->closure_ontology_mask & (1u << node)) {
      bitset_set(&engine->by_closure_ontology[node], slot);
    }
  }

  BitSet *src_cb = country_fiber_get(engine->by_source_country, 256u, a->feed_country, 1);
  if(src_cb) bitset_set(src_cb, slot);

  for(size_t i = 0; i < a->mentioned_country_count; ++i) {
    BitSet *m_cb = country_fiber_get(engine->by_mentioned_country, 256u, a->mentioned_countries[i], 1);
    if(m_cb) bitset_set(m_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_asset_count; ++i) {
    BitSet *a_cb = country_fiber_get(engine->by_asset, 128u, a->mentioned_assets[i], 1);
    if(a_cb) bitset_set(a_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_fiscal_event_count; ++i) {
    BitSet *e_cb = country_fiber_get(engine->by_fiscal_event, 64u, a->mentioned_fiscal_events[i], 1);
    if(e_cb) bitset_set(e_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_cve_count; ++i) {
    BitSet *c_cb = country_fiber_get(engine->by_cve, 128u, a->mentioned_cves[i], 1);
    if(c_cb) bitset_set(c_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_org_count; ++i) {
    BitSet *o_cb = country_fiber_get(engine->by_org, 128u, a->mentioned_orgs[i], 1);
    if(o_cb) bitset_set(o_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_ai_model_count; ++i) {
    BitSet *m_cb = country_fiber_get(engine->by_ai_model, 128u, a->mentioned_ai_models[i], 1);
    if(m_cb) bitset_set(m_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_ai_infra_count; ++i) {
    BitSet *i_cb = country_fiber_get(engine->by_ai_infra, 128u, a->mentioned_ai_infra[i], 1);
    if(i_cb) bitset_set(i_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_hf_repo_count; ++i) {
    BitSet *h_cb = country_fiber_get(engine->by_hf_repo, 128u, a->mentioned_hf_repos[i], 1);
    if(h_cb) bitset_set(h_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_sport_count; ++i) {
    BitSet *sp_cb = country_fiber_get(engine->by_sport, 128u, a->mentioned_sports[i], 1);
    if(sp_cb) bitset_set(sp_cb, slot);
  }
  for(size_t i = 0; i < a->mentioned_weather_count; ++i) {
    BitSet *wx_cb = country_fiber_get(engine->by_weather, 128u, a->mentioned_weather[i], 1);
    if(wx_cb) bitset_set(wx_cb, slot);
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
  a->m1_sketch64 = m1_structural_sketch64(a->title, strlen(a->title), &a->m1_landmark_hits);

  /* Insert into 4-band x 16-bit Multi-Probe LSH index */
  for(int b = 0; b < 4; ++b) {
    if(!engine->lsh_bands[b]) continue;
    uint16_t band_val = (uint16_t)((a->simhash64 >> (b * 16)) & 0xFFFFu);
    size_t b_idx = (size_t)(band_val & 0x0FFFu);
    SimHashBucket *sb = &engine->lsh_bands[b][b_idx];
    if(sb->count == sb->cap) {
      uint32_t ncap = sb->cap ? (sb->cap * 2u) : 4u;
      uint32_t *nslots = realloc(sb->slots, ncap * sizeof(uint32_t));
      if(nslots) {
        sb->slots = nslots;
        sb->cap = ncap;
      }
    }
    if(sb->count < sb->cap) {
      sb->slots[sb->count++] = slot;
    }
  }

  for(size_t i = 0; i < term_count; ++i) {
    inv_post(engine, terms[i].term, terms[i].hash, slot, terms[i].weight, terms[i].pos_mask);
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
  if(feed_id > 0 && (size_t)(feed_id - 1u) < engine->feed_count && engine->feeds[feed_id - 1u].id == feed_id) {
    feed = &engine->feeds[feed_id - 1u];
  } else {
    for(size_t i = 0; i < engine->feed_count; ++i) {
      if(engine->feeds[i].id == feed_id) {
        feed = &engine->feeds[i];
        break;
      }
    }
  }
  if(!feed) return -1;

  const char *eff_guid = (guid && guid[0]) ? guid : ((url && url[0]) ? url : title);
  uint64_t guid_h = wyhash64((uint64_t)feed_id, fnv1a64(eff_guid, strlen(eff_guid)));
  uint32_t existing_id = dedup_lookup(engine, guid_h);
  if(existing_id != 0u) {
    if(out_article_id) *out_article_id = existing_id;
    return 0;
  }
  uint64_t url_h = 0ULL;
  if(url && url[0]) {
    url_h = wyhash64(0x9E3779B97F4A7C15ULL, fnv1a64(url, strlen(url)));
    existing_id = dedup_lookup(engine, url_h);
    if(existing_id != 0u) {
      if(out_article_id) *out_article_id = existing_id;
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
  dedup_insert(engine, guid_h, a->id);
  if(url_h != 0ULL) dedup_insert(engine, url_h, a->id);
  a->guid = arena_strdup(engine, eff_guid);
  a->title = arena_strdup(engine, title);
  a->url = arena_strdup(engine, url ? url : "");
  a->author = arena_strdup(engine, author ? author : "");
  a->summary = arena_strdup(engine, summary ? summary : "");
  a->content_html = arena_strdup(engine, content_html ? content_html : "");
  a->content_text = markup_clean_cstr((content_html && content_html[0]) ? content_html : summary);
  if(!a->content_text) a->content_text = dup_str("");
  a->published_at = arena_strdup(engine, published_at ? published_at : "2026-11-03T00:00:00Z");
  a->published_ms = published_ms > 0 ? published_ms : wall_time_ms();
  a->image_url = arena_strdup(engine, image_url ? image_url : "");
  a->feed_title = arena_strdup(engine, feed->title);
  a->feed_category = arena_strdup(engine, feed->category);
  a->feed_region = feed->region;
  strcpy(a->feed_country, feed->country);
  a->feed_language = arena_strdup(engine, feed->language);
  a->feed_source_tier = arena_strdup(engine, feed->source_tier);

  ExtractedEntities ent;
  entities_extract(a->title, a->summary, a->content_text, a->feed_category, tags, tag_count, &ent);
  a->news_type = ent.news_type;
  a->clearance = ent.clearance;
  a->opsec_score = ent.opsec_score;
  a->fiscal_stance = ent.fiscal_stance;
  a->macro_score = ent.macro_score;
  a->ai_tier = ent.ai_tier;
  a->ai_compute_score = ent.ai_compute_score;
  a->ai_license = ent.ai_license;
  a->openness_score = ent.openness_score;
  a->sports_score = ent.sports_score;
  a->weather_severity = ent.weather_severity;
  a->weather_score = ent.weather_score;
  a->direct_ontology_mask = ent.direct_ontology_mask;
  a->closure_ontology_mask = ent.closure_ontology_mask;

  for(size_t i = 0; i < ent.cve_count && a->mentioned_cve_count < MAX_EXTRACTED_CVES; ++i) {
    strcpy(a->mentioned_cves[a->mentioned_cve_count++], ent.cves[i]);
  }
  for(size_t i = 0; i < ent.asset_count && a->mentioned_asset_count < MAX_EXTRACTED_ASSETS; ++i) {
    strcpy(a->mentioned_assets[a->mentioned_asset_count++], ent.assets[i]);
  }
  for(size_t i = 0; i < ent.fiscal_event_count && a->mentioned_fiscal_event_count < MAX_EXTRACTED_EVENTS; ++i) {
    strcpy(a->mentioned_fiscal_events[a->mentioned_fiscal_event_count++], ent.fiscal_events[i]);
  }
  for(size_t i = 0; i < ent.ai_model_count && a->mentioned_ai_model_count < MAX_EXTRACTED_AI_MODELS; ++i) {
    strcpy(a->mentioned_ai_models[a->mentioned_ai_model_count++], ent.ai_models[i]);
  }
  for(size_t i = 0; i < ent.ai_infra_count && a->mentioned_ai_infra_count < MAX_EXTRACTED_AI_INFRA; ++i) {
    strcpy(a->mentioned_ai_infra[a->mentioned_ai_infra_count++], ent.ai_infra[i]);
  }
  for(size_t i = 0; i < ent.hf_repo_count && a->mentioned_hf_repo_count < MAX_EXTRACTED_HF_REPOS; ++i) {
    strcpy(a->mentioned_hf_repos[a->mentioned_hf_repo_count++], ent.hf_repos[i]);
  }
  for(size_t i = 0; i < ent.sport_count && a->mentioned_sport_count < MAX_EXTRACTED_SPORTS; ++i) {
    strcpy(a->mentioned_sports[a->mentioned_sport_count++], ent.sports[i]);
  }
  for(size_t i = 0; i < ent.weather_count && a->mentioned_weather_count < MAX_EXTRACTED_WEATHER; ++i) {
    strcpy(a->mentioned_weather[a->mentioned_weather_count++], ent.weather[i]);
  }

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
  for(const char *p = a->content_text ? a->content_text : ""; *p; ++p) {
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
  evaluate_article_automation_rules(engine, a);
  pthread_mutex_lock(&engine->cache_lock);
  engine->index_generation++;
  for(size_t c = 0; c < 64u; ++c) {
    if(engine->query_cache[c].valid) {
      free(engine->query_cache[c].slots);
      free(engine->query_cache[c].scores);
      engine->query_cache[c].slots = NULL;
      engine->query_cache[c].scores = NULL;
      engine->query_cache[c].valid = 0;
    }
  }
  pthread_mutex_unlock(&engine->cache_lock);

  if(out_article_id) *out_article_id = a->id;
  if(out_inserted) *out_inserted = 1;
  return 0;
}

const ArticleRecord *engine_get_article_by_id(const OyezEngine *engine, uint32_t id) {
  if(!engine || id == 0) return NULL;
  if((size_t)(id - 1u) < engine->article_count && engine->articles[id - 1u].id == id) {
    return &engine->articles[id - 1u];
  }
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
  uint32_t m1_hamming = simhash64_distance(a->m1_sketch64, b->m1_sketch64);
  double sim_hash = 1.0 - ((double)hamming / 64.0);
  double sim_m1 = 1.0 - ((double)m1_hamming / 64.0);
  double sim_onto = 1.0 - ontology_wasserstein_distance(a->direct_ontology_mask, b->direct_ontology_mask);
  double same_feed = (a->feed_id == b->feed_id) ? 1.0 : 0.0;
  return 0.35 * sim_hash + 0.15 * sim_m1 + 0.30 * sim_onto + 0.20 * same_feed;
}

static uint64_t hash_exact_cstr(const char *s) {
  return fnv1a64(s ? s : "", s ? strlen(s) : 0u);
}

static uint64_t hash_article_query(const ArticleQuery *q, uint64_t gen) {
  uint64_t h = mix64(gen ^ 0x9E3779B97F4A7C15ULL);
  h ^= mix64(hash_exact_cstr(q->query ? q->query : "") + 1u);
  h ^= mix64(hash_exact_cstr(q->category ? q->category : "") + 2u);
  h ^= mix64(hash_exact_cstr(q->exclude_category ? q->exclude_category : "") + 21u);
  h ^= mix64(hash_exact_cstr(q->news_type ? q->news_type : "") + 3u);
  h ^= mix64(hash_exact_cstr(q->clearance ? q->clearance : "") + 4u);
  h ^= mix64(hash_exact_cstr(q->fiscal_stance ? q->fiscal_stance : "") + 5u);
  h ^= mix64(hash_exact_cstr(q->asset ? q->asset : "") + 6u);
  h ^= mix64(hash_exact_cstr(q->fiscal_event ? q->fiscal_event : "") + 7u);
  h ^= mix64(hash_exact_cstr(q->cve ? q->cve : "") + 13u);
  h ^= mix64(hash_exact_cstr(q->organization ? q->organization : "") + 14u);
  h ^= mix64(hash_exact_cstr(q->ai_tier ? q->ai_tier : "") + 15u);
  h ^= mix64(hash_exact_cstr(q->ai_model ? q->ai_model : "") + 16u);
  h ^= mix64(hash_exact_cstr(q->ai_infra ? q->ai_infra : "") + 17u);
  h ^= mix64(hash_exact_cstr(q->ai_license ? q->ai_license : "") + 18u);
  h ^= mix64(hash_exact_cstr(q->hf_repo ? q->hf_repo : "") + 19u);
  h ^= mix64(hash_exact_cstr(q->sport ? q->sport : "") + 20u);
  h ^= mix64(hash_exact_cstr(q->weather ? q->weather : "") + 22u);
  h ^= mix64(hash_exact_cstr(q->weather_severity ? q->weather_severity : "") + 23u);
  h ^= mix64(hash_exact_cstr(q->region ? q->region : "") + 8u);
  h ^= mix64(hash_exact_cstr(q->country ? q->country : "") + 9u);
  h ^= mix64(hash_exact_cstr(q->mentioned_country ? q->mentioned_country : "") + 10u);
  h ^= mix64(hash_exact_cstr(q->language ? q->language : "") + 11u);
  h ^= mix64(hash_exact_cstr(q->source_tier ? q->source_tier : "") + 12u);
  double lambda = (q->mmr_lambda > 0.0 && q->mmr_lambda <= 1.0) ? q->mmr_lambda : 0.65;
  uint64_t lambda_bits = 0;
  memcpy(&lambda_bits, &lambda, sizeof(lambda_bits));
  size_t norm_limit = q->limit > 0 ? q->limit : 50u;
  if(norm_limit > 500u) norm_limit = 500u;
  h ^= mix64(((uint64_t)q->feed_id << 32u) | ((uint64_t)(q->use_category_morphism != 0) << 16u) |
             ((uint64_t)(q->diversify_mmr & 3) << 14u));
  h ^= mix64(lambda_bits ^ 0xB5AD4ECEDA1CE2A9ULL);
  h ^= mix64((uint64_t)norm_limit ^ 0xA57B57D5A57B57D5ULL);
  h ^= mix64((uint64_t)q->offset ^ 0xC6A4A7935BD1E995ULL);
  return h;
}

int engine_search(const OyezEngine *engine, const ArticleQuery *q, SearchResponse *out) {
  if(!engine || !q || !out) return -1;
  memset(out, 0, sizeof(*out));
  uint64_t t0 = monotonic_ns();

  OyezEngine *mut_eng = (OyezEngine *)engine;
  uint64_t qhash = hash_article_query(q, engine->index_generation);
  size_t cslot = (size_t)(qhash & 63ULL);
  pthread_mutex_lock(&mut_eng->cache_lock);
  if(engine->query_cache[cslot].valid && engine->query_cache[cslot].key_hash == qhash) {
    size_t take = engine->query_cache[cslot].count;
    out->total = engine->query_cache[cslot].total;
    out->count = take;
    if(take > 0) {
      out->hits = malloc(take * sizeof(ScoredArticle));
      if(!out->hits) {
        pthread_mutex_unlock(&mut_eng->cache_lock);
        return -1;
      }
      for(size_t i = 0; i < take; ++i) {
        out->hits[i].article = &engine->articles[engine->query_cache[cslot].slots[i]];
        out->hits[i].score = (double)engine->query_cache[cslot].scores[i];
      }
    }
    pthread_mutex_unlock(&mut_eng->cache_lock);
    out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
    return 0;
  }
  pthread_mutex_unlock(&mut_eng->cache_lock);

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

  if(q->clearance && q->clearance[0]) {
    ClearanceLevel cl;
    if(clearance_level_from_slug(q->clearance, &cl) == 0) {
      bitset_and_inplace(&candidates, &engine->by_clearance[cl]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->fiscal_stance && q->fiscal_stance[0]) {
    FiscalStance fs;
    if(fiscal_stance_from_slug(q->fiscal_stance, &fs) == 0) {
      bitset_and_inplace(&candidates, &engine->by_fiscal_stance[fs]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->asset && q->asset[0]) {
    const BitSet *ab = country_fiber_get((CountryFiber *)engine->by_asset, 128u, q->asset, 0);
    if(ab) bitset_and_inplace(&candidates, ab);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->fiscal_event && q->fiscal_event[0]) {
    const BitSet *eb = country_fiber_get((CountryFiber *)engine->by_fiscal_event, 64u, q->fiscal_event, 0);
    if(eb) bitset_and_inplace(&candidates, eb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->cve && q->cve[0]) {
    const BitSet *cvb = country_fiber_get((CountryFiber *)engine->by_cve, 128u, q->cve, 0);
    if(cvb) bitset_and_inplace(&candidates, cvb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->organization && q->organization[0]) {
    const BitSet *ob = country_fiber_get((CountryFiber *)engine->by_org, 128u, q->organization, 0);
    if(ob) bitset_and_inplace(&candidates, ob);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->ai_tier && q->ai_tier[0]) {
    AiComputeTier at;
    if(ai_compute_tier_from_slug(q->ai_tier, &at) == 0) {
      bitset_and_inplace(&candidates, &engine->by_ai_tier[at]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->ai_model && q->ai_model[0]) {
    const BitSet *mb = country_fiber_get((CountryFiber *)engine->by_ai_model, 128u, q->ai_model, 0);
    if(mb) bitset_and_inplace(&candidates, mb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->ai_infra && q->ai_infra[0]) {
    const BitSet *ib = country_fiber_get((CountryFiber *)engine->by_ai_infra, 128u, q->ai_infra, 0);
    if(ib) bitset_and_inplace(&candidates, ib);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->ai_license && q->ai_license[0]) {
    AiWeightLicense al;
    if(ai_weight_license_from_slug(q->ai_license, &al) == 0) {
      bitset_and_inplace(&candidates, &engine->by_ai_license[al]);
    } else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->hf_repo && q->hf_repo[0]) {
    const BitSet *hb = country_fiber_get((CountryFiber *)engine->by_hf_repo, 128u, q->hf_repo, 0);
    if(hb) bitset_and_inplace(&candidates, hb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->sport && q->sport[0]) {
    const BitSet *spb = country_fiber_get((CountryFiber *)engine->by_sport, 128u, q->sport, 0);
    if(spb) bitset_and_inplace(&candidates, spb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->weather && q->weather[0]) {
    const BitSet *wxb = country_fiber_get((CountryFiber *)engine->by_weather, 128u, q->weather, 0);
    if(wxb) bitset_and_inplace(&candidates, wxb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->weather_severity && q->weather_severity[0]) {
    WeatherSeverity ws;
    if(weather_severity_from_slug(q->weather_severity, &ws) == 0) {
      bitset_and_inplace(&candidates, &engine->by_weather_severity[ws]);
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

  if(q->exclude_category && q->exclude_category[0]) {
    OntologyNodeId ex_node;
    if(ontology_from_slug(q->exclude_category, &ex_node) == 0) {
      bitset_andnot_inplace(&candidates, &engine->by_closure_ontology[ex_node]);
    }
  }

  if(q->country && q->country[0]) {
    const BitSet *cb = country_fiber_get((CountryFiber *)engine->by_source_country, 256u, q->country, 0);
    if(cb) bitset_and_inplace(&candidates, cb);
    else {
      bitset_free(&candidates);
      return 0;
    }
  }

  if(q->mentioned_country && q->mentioned_country[0]) {
    const BitSet *mcb = country_fiber_get((CountryFiber *)engine->by_mentioned_country, 256u, q->mentioned_country, 0);
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
    const double delta_plus = 1.0; /* BM25+ lower-bound TF normalization */

    uint32_t *prev_pos = (qterm_count > 1u) ? calloc(engine->article_count, sizeof(uint32_t)) : NULL;

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
        double term_bm25 = idf * (((tf * (k1 + 1.0)) / denom) + delta_plus);
        if(prev_pos) {
          uint32_t pm = bucket->pos_masks ? bucket->pos_masks[p] : 0u;
          uint32_t prev_pm = prev_pos[slot];
          if(prev_pm != 0u && ((pm & prev_pm) != 0u || (pm & (prev_pm << 1u)) != 0u)) {
            term_bm25 *= 1.35; /* Positional phrase / proximity boost */
          }
          prev_pos[slot] = pm;
        }
        bm25_scores[slot] += term_bm25;
        bitset_set(&query_union, slot);
      }
    }
    free(prev_pos);
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

      double spec_boost = 1.0 + 0.05 * (ontology_information_specificity(art->direct_ontology_mask) - 1.0);
      double sc = bm25_scores ? (bm25_scores[slot] * spec_boost)
                              : (double)(art->published_ms % 100000000ULL) * 1e-6 + spec_boost;
      pool[pool_count].article = art;
      pool[pool_count].score = sc;
      pool_count++;
    }
  }

  free(bm25_scores);
  bitset_free(&candidates);

  qsort(pool, pool_count, sizeof(ScoredArticle), compare_scored_desc);

  size_t limit = q->limit > 0 ? q->limit : 50u;
  if(limit > 500u) limit = 500u;
  size_t offset = q->offset;
  out->total = pool_count;

  if(offset >= pool_count) {
    free(pool);
    out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
    return 0;
  }

  if(q->diversify_mmr == 2 && pool_count > 1u) {
    engine_dpp_rerank(pool, pool_count, q->mmr_lambda);
  } else if(q->diversify_mmr && pool_count > 1u) {
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
        int found_best = 0;
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
          if(!found_best || mmr > best_mmr) {
            best_mmr = mmr;
            best_idx = i;
            found_best = 1;
          }
        }
        if(!found_best) break;
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

  /* Populate 64-entry query response cache under cache_lock */
  pthread_mutex_lock(&mut_eng->cache_lock);
  if(mut_eng->query_cache[cslot].valid) {
    free(mut_eng->query_cache[cslot].slots);
    free(mut_eng->query_cache[cslot].scores);
    mut_eng->query_cache[cslot].slots = NULL;
    mut_eng->query_cache[cslot].scores = NULL;
    mut_eng->query_cache[cslot].valid = 0;
  }
  mut_eng->query_cache[cslot].slots = malloc((take ? take : 1u) * sizeof(uint32_t));
  mut_eng->query_cache[cslot].scores = malloc((take ? take : 1u) * sizeof(float));
  if(mut_eng->query_cache[cslot].slots && mut_eng->query_cache[cslot].scores) {
    for(size_t i = 0; i < take; ++i) {
      mut_eng->query_cache[cslot].slots[i] = out->hits[i].article->slot;
      mut_eng->query_cache[cslot].scores[i] = (float)out->hits[i].score;
    }
    mut_eng->query_cache[cslot].key_hash = qhash;
    mut_eng->query_cache[cslot].count = take;
    mut_eng->query_cache[cslot].total = pool_count;
    mut_eng->query_cache[cslot].valid = 1;
  } else {
    free(mut_eng->query_cache[cslot].slots);
    free(mut_eng->query_cache[cslot].scores);
    mut_eng->query_cache[cslot].slots = NULL;
    mut_eng->query_cache[cslot].scores = NULL;
  }
  pthread_mutex_unlock(&mut_eng->cache_lock);

  free(pool);
  out->query_time_ms = (double)(monotonic_ns() - t0) / 1e6;
  return 0;
}

void search_response_free(SearchResponse *res) {
  if(!res) return;
  free(res->hits);
  memset(res, 0, sizeof(*res));
}

double engine_dpp_rerank(ScoredArticle *pool, size_t count, double lambda) {
  /* Fast Greedy Determinantal Point Process (DPP) MAP Inference (Chen et al. NeurIPS 2018)
   * via Incremental Cholesky Orthogonalization:
   * Given quality scores q_i and positive-definite Gram similarity kernel S_{i,j},
   * constructs L_{i,j} = q_i * S_{i,j} * q_j + eps * delta_{i,j} and greedily maximizes
   * the parallelotope volume log det(L_Y) = sum_{t=0}^{W-1} ln(d_{j_t^*}^2) in O(W^3). */
  if(!pool || count == 0u) return 0.0;
  size_t W = count < 48u ? count : 48u;
  double max_rel = pool[0].score > 0.0 ? pool[0].score : 1.0;
  double lam = (lambda > 0.0 && lambda <= 1.0) ? lambda : 0.65;

  double q_vec[48];
  double d_sq[48];
  double chol[48][48];
  uint8_t selected[48] = {0};
  ScoredArticle reranked[48];
  memset(chol, 0, sizeof(chol));

  for(size_t i = 0; i < W; ++i) {
    double norm_r = pool[i].score > 0.0 ? (pool[i].score / max_rel) : 0.01;
    q_vec[i] = 0.5 + lam * norm_r;
    d_sq[i] = q_vec[i] * q_vec[i] * 1.02; /* Diagonal L_{i,i} with Tikhonov regularization */
  }

  double log_det = 0.0;
  for(size_t t = 0; t < W; ++t) {
    size_t best_j = 0;
    double best_d2 = -1.0;
    for(size_t i = 0; i < W; ++i) {
      if(!selected[i] && d_sq[i] > best_d2) {
        best_d2 = d_sq[i];
        best_j = i;
      }
    }
    if(best_d2 <= 1e-12) break;
    selected[best_j] = 1u;
    reranked[t] = pool[best_j];
    log_det += log(best_d2);
    double sigma = sqrt(best_d2);
    double inv_sigma = 1.0 / sigma;

    for(size_t i = 0; i < W; ++i) {
      if(selected[i]) continue;
      double s_ji = (1.0 - lam) * article_similarity(pool[best_j].article, pool[i].article);
      double l_ji = q_vec[best_j] * s_ji * q_vec[i];
      double dot = 0.0;
      for(size_t s = 0; s < t; ++s) {
        dot += chol[best_j][s] * chol[i][s];
      }
      double e_i = (l_ji - dot) * inv_sigma;
      chol[i][t] = e_i;
      double next_d2 = d_sq[i] - e_i * e_i;
      d_sq[i] = next_d2 > 1e-12 ? next_d2 : 1e-12;
    }
  }
  memcpy(pool, reranked, W * sizeof(ScoredArticle));
  return log_det;
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

  /* Probe 4-band LSH buckets + 1-bit multi-probe neighbors first */
  BitSet visited;
  bitset_init(&visited);
  bitset_set(&visited, target->slot);

  for(int b = 0; b < 4 && found < max_out; ++b) {
    if(!engine->lsh_bands[b]) continue;
    uint16_t band_val = (uint16_t)((target->simhash64 >> (b * 16)) & 0xFFFFu);
    for(int probe = -1; probe < 12 && found < max_out; ++probe) {
      uint16_t pval = (probe < 0) ? band_val : (uint16_t)(band_val ^ (1u << probe));
      const SimHashBucket *sb = &engine->lsh_bands[b][pval & 0x0FFFu];
      for(uint32_t k = 0; k < sb->count && found < max_out; ++k) {
        uint32_t slot = sb->slots[k];
        if(bitset_test(&visited, slot)) continue;
        bitset_set(&visited, slot);
        const ArticleRecord *cand = &engine->articles[slot];
        uint32_t dist = simhash64_distance(target->simhash64, cand->simhash64);
        if(dist <= max_hamming) {
          out_articles[found] = cand;
          if(out_distances) out_distances[found] = dist;
          found++;
        }
      }
    }
  }

  /* Fallback linear scan if corpus is small and caller wants wider radius */
  if(found < max_out && engine->article_count <= 2048u) {
    for(size_t i = 0; i < engine->article_count && found < max_out; ++i) {
      uint32_t slot = (uint32_t)i;
      if(bitset_test(&visited, slot)) continue;
      const ArticleRecord *cand = &engine->articles[i];
      uint32_t dist = simhash64_distance(target->simhash64, cand->simhash64);
      if(dist <= max_hamming) {
        out_articles[found] = cand;
        if(out_distances) out_distances[found] = dist;
        found++;
      }
    }
  }
  bitset_free(&visited);
  return found;
}

size_t engine_story_clusters(const OyezEngine *engine,
                             uint32_t max_hamming,
                             StoryCluster *out_clusters,
                             size_t max_clusters) {
  if(!engine || !out_clusters || max_clusters == 0 || engine->article_count == 0) return 0;
  BitSet assigned;
  bitset_init(&assigned);
  size_t ccount = 0;

  for(size_t i = 0; i < engine->article_count && ccount < max_clusters; ++i) {
    const ArticleRecord *lead = &engine->articles[i];
    if(bitset_test(&assigned, lead->slot)) continue;

    const ArticleRecord *dups[15];
    uint32_t dists[15];
    size_t nd = engine_find_near_duplicates(engine, lead->id, max_hamming, dups, dists, 15u);

    StoryCluster cl;
    memset(&cl, 0, sizeof(cl));
    cl.cluster_id = (uint32_t)(ccount + 1u);
    cl.lead_article = lead;
    cl.members[cl.member_count++] = lead;
    cl.region_mask = (1u << (uint32_t)lead->feed_region);
    cl.ontology_mask = lead->direct_ontology_mask;
    bitset_set(&assigned, lead->slot);

    for(size_t k = 0; k < nd && cl.member_count < 16u; ++k) {
      if(bitset_test(&assigned, dups[k]->slot)) continue;
      bitset_set(&assigned, dups[k]->slot);
      cl.members[cl.member_count++] = dups[k];
      cl.region_mask |= (1u << (uint32_t)dups[k]->feed_region);
      cl.ontology_mask |= dups[k]->direct_ontology_mask;
    }

    cl.cluster_score = (double)cl.member_count * 2.5 +
                       (double)popcnt64((uint64_t)cl.region_mask) * 1.8;
    out_clusters[ccount++] = cl;
  }
  bitset_free(&assigned);
  return ccount;
}

static void maybe_add_formal_concept(const OyezEngine *engine,
                                     const BitSet *ext,
                                     size_t min_support,
                                     FormalConcept *out_concepts,
                                     size_t *count,
                                     size_t max_concepts) {
  if(*count >= max_concepts) return;
  uint32_t supp = bitset_popcount(ext);
  if(supp < min_support) return;

  uint32_t intent_onto = 0xFFFFFFFFu;
  uint32_t intent_reg = 0xFFFFFFFFu;
  uint32_t intent_nt = 0xFFFFFFFFu;
  FormalConcept fc;
  memset(&fc, 0, sizeof(fc));

  for(size_t w = 0; w < ext->word_cap; ++w) {
    uint64_t word = ext->words[w];
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

  for(size_t k = 0; k < *count; ++k) {
    if(out_concepts[k].intent_ontology_mask == intent_onto &&
       out_concepts[k].extent_count == fc.extent_count) {
      return;
    }
  }

  fc.concept_id = (uint32_t)(*count);
  fc.intent_ontology_mask = intent_onto;
  fc.lawvere_tierney_mask = ontology_lawvere_tierney_sheafify(intent_onto);
  fc.intent_region_mask = intent_reg;
  fc.intent_news_type_mask = intent_nt;
  fc.intensional_entropy = ontology_information_specificity(intent_onto);
  fc.stability_score = (double)supp * (double)popcnt64((uint64_t)intent_onto) * (1.0 + 0.1 * fc.intensional_entropy);
  out_concepts[(*count)++] = fc;
}

size_t engine_formal_concepts(const OyezEngine *engine,
                              size_t min_support,
                              FormalConcept *out_concepts,
                              size_t max_concepts) {
  if(!engine || !out_concepts || max_concepts == 0 || engine->article_count == 0) return 0;
  if(min_support < 1u) min_support = 1u;

  size_t count = 0;
  /* 1. Single-attribute Galois closures */
  for(uint32_t a = 1u; a < ONTOLOGY_NODE_COUNT && count < max_concepts; ++a) {
    maybe_add_formal_concept(engine, &engine->by_closure_ontology[a], min_support, out_concepts, &count, max_concepts);
  }
  /* 2. Pairwise-attribute intersection Galois closures (A ^ B)' -> (A ^ B)'' */
  BitSet inter;
  bitset_init(&inter);
  for(uint32_t a = 5u; a < ONTOLOGY_NODE_COUNT && count < max_concepts; ++a) {
    if(bitset_popcount(&engine->by_closure_ontology[a]) < min_support) continue;
    for(uint32_t b = a + 1u; b < ONTOLOGY_NODE_COUNT && count < max_concepts; ++b) {
      if(ontology_has_morphism((OntologyNodeId)a, (OntologyNodeId)b) ||
         ontology_has_morphism((OntologyNodeId)b, (OntologyNodeId)a)) {
        continue;
      }
      if(bitset_popcount(&engine->by_closure_ontology[b]) < min_support) continue;
      if(bitset_clone(&engine->by_closure_ontology[a], &inter) != 0) continue;
      bitset_and_inplace(&inter, &engine->by_closure_ontology[b]);
      maybe_add_formal_concept(engine, &inter, min_support, out_concepts, &count, max_concepts);
    }
  }
  bitset_free(&inter);

  /* 3. Compute Galois Concept Lattice Hasse Diagram covering morphisms:
   * Concept i <= Concept j iff Intent(j) is a strict subset of Intent(i)
   * (equivalently Extent(i) is a subset of Extent(j)), with no intermediate Concept k. */
  size_t hasse_cap = count < 32u ? count : 32u;
  for(size_t i = 0; i < hasse_cap; ++i) {
    uint32_t mi = out_concepts[i].intent_ontology_mask;
    for(size_t j = 0; j < hasse_cap; ++j) {
      if(i == j) continue;
      uint32_t mj = out_concepts[j].intent_ontology_mask;
      /* Check if i is strictly more specific than j (mj is a strict subset of mi) */
      if(mi != mj && (mi & mj) == mj) {
        int has_intermediate = 0;
        for(size_t k = 0; k < hasse_cap; ++k) {
          if(k == i || k == j) continue;
          uint32_t mk = out_concepts[k].intent_ontology_mask;
          if(mk != mi && mk != mj && (mi & mk) == mk && (mk & mj) == mj) {
            has_intermediate = 1;
            break;
          }
        }
        if(!has_intermediate) {
          out_concepts[i].superconcept_mask |= (1u << (uint32_t)j);
          out_concepts[j].subconcept_mask |= (1u << (uint32_t)i);
        }
      }
    }
  }
  return count;
}

static const char *match_article_witness(const ArticleRecord *a,
                                         const ArticleRecord *b,
                                         const char *pivot_dimension) {
  int by_country = (pivot_dimension && strcmp(pivot_dimension, "mentioned_country") == 0);
  int by_type = (pivot_dimension && strcmp(pivot_dimension, "news_type") == 0);
  int by_asset = (pivot_dimension && strcmp(pivot_dimension, "asset") == 0);
  int by_org = (pivot_dimension && strcmp(pivot_dimension, "organization") == 0);
  int by_cve = (pivot_dimension && strcmp(pivot_dimension, "cve") == 0);
  int by_event = (pivot_dimension && strcmp(pivot_dimension, "fiscal_event") == 0);
  int by_ai_model = (pivot_dimension && strcmp(pivot_dimension, "ai_model") == 0);
  int by_ai_infra = (pivot_dimension && strcmp(pivot_dimension, "ai_infra") == 0);
  int by_hf_repo = (pivot_dimension && strcmp(pivot_dimension, "hf_repo") == 0);
  int by_ai_license = (pivot_dimension && strcmp(pivot_dimension, "ai_license") == 0);
  int by_sport = (pivot_dimension && strcmp(pivot_dimension, "sport") == 0);

  if(by_country) {
    for(size_t ca = 0; ca < a->mentioned_country_count; ++ca) {
      for(size_t cb = 0; cb < b->mentioned_country_count; ++cb) {
        if(strcmp(a->mentioned_countries[ca], b->mentioned_countries[cb]) == 0) {
          return a->mentioned_countries[ca];
        }
      }
    }
  } else if(by_asset) {
    for(size_t aa = 0; aa < a->mentioned_asset_count; ++aa) {
      for(size_t ab = 0; ab < b->mentioned_asset_count; ++ab) {
        if(strcmp(a->mentioned_assets[aa], b->mentioned_assets[ab]) == 0) {
          return a->mentioned_assets[aa];
        }
      }
    }
  } else if(by_org) {
    for(size_t oa = 0; oa < a->mentioned_org_count; ++oa) {
      for(size_t ob = 0; ob < b->mentioned_org_count; ++ob) {
        if(strcmp(a->mentioned_orgs[oa], b->mentioned_orgs[ob]) == 0) {
          return a->mentioned_orgs[oa];
        }
      }
    }
  } else if(by_cve) {
    for(size_t ca = 0; ca < a->mentioned_cve_count; ++ca) {
      for(size_t cb = 0; cb < b->mentioned_cve_count; ++cb) {
        if(strcmp(a->mentioned_cves[ca], b->mentioned_cves[cb]) == 0) {
          return a->mentioned_cves[ca];
        }
      }
    }
  } else if(by_event) {
    for(size_t ea = 0; ea < a->mentioned_fiscal_event_count; ++ea) {
      for(size_t eb = 0; eb < b->mentioned_fiscal_event_count; ++eb) {
        if(strcmp(a->mentioned_fiscal_events[ea], b->mentioned_fiscal_events[eb]) == 0) {
          return a->mentioned_fiscal_events[ea];
        }
      }
    }
  } else if(by_ai_model) {
    for(size_t ma = 0; ma < a->mentioned_ai_model_count; ++ma) {
      for(size_t mb = 0; mb < b->mentioned_ai_model_count; ++mb) {
        if(strcmp(a->mentioned_ai_models[ma], b->mentioned_ai_models[mb]) == 0) {
          return a->mentioned_ai_models[ma];
        }
      }
    }
  } else if(by_ai_infra) {
    for(size_t ia = 0; ia < a->mentioned_ai_infra_count; ++ia) {
      for(size_t ib = 0; ib < b->mentioned_ai_infra_count; ++ib) {
        if(strcmp(a->mentioned_ai_infra[ia], b->mentioned_ai_infra[ib]) == 0) {
          return a->mentioned_ai_infra[ia];
        }
      }
    }
  } else if(by_hf_repo) {
    for(size_t ha = 0; ha < a->mentioned_hf_repo_count; ++ha) {
      for(size_t hb = 0; hb < b->mentioned_hf_repo_count; ++hb) {
        if(strcmp(a->mentioned_hf_repos[ha], b->mentioned_hf_repos[hb]) == 0) {
          return a->mentioned_hf_repos[ha];
        }
      }
    }
  } else if(by_ai_license) {
    if(a->ai_license != AI_LICENSE_UNSPECIFIED && a->ai_license == b->ai_license) {
      return ai_weight_license_slug(a->ai_license);
    }
  } else if(by_sport) {
    for(size_t sa = 0; sa < a->mentioned_sport_count; ++sa) {
      for(size_t sb = 0; sb < b->mentioned_sport_count; ++sb) {
        if(strcmp(a->mentioned_sports[sa], b->mentioned_sports[sb]) == 0) {
          return a->mentioned_sports[sa];
        }
      }
    }
  } else if(by_type) {
    if(a->news_type == b->news_type) {
      return news_type_slug(a->news_type);
    }
  } else {
    uint32_t shared = (a->direct_ontology_mask & b->direct_ontology_mask) & ~1u;
    if(shared) {
      uint32_t node = ctz64((uint64_t)shared);
      return ontology_node((OntologyNodeId)node)->slug;
    }
  }
  return NULL;
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
  const BitSet *bs_l = &engine->by_region[lr];
  const BitSet *bs_r = &engine->by_region[rr];

  for(size_t wl = 0; wl < bs_l->word_cap && found < max_pairs; ++wl) {
    uint64_t word_l = bs_l->words[wl];
    while(word_l && found < max_pairs) {
      uint32_t bit_l = ctz64(word_l);
      uint32_t slot_l = (uint32_t)(wl * 64u + bit_l);
      word_l &= (word_l - 1ULL);
      if(slot_l >= engine->article_count) continue;
      const ArticleRecord *a = &engine->articles[slot_l];

      for(size_t wr = 0; wr < bs_r->word_cap && found < max_pairs; ++wr) {
        uint64_t word_r = bs_r->words[wr];
        while(word_r && found < max_pairs) {
          uint32_t bit_r = ctz64(word_r);
          uint32_t slot_r = (uint32_t)(wr * 64u + bit_r);
          word_r &= (word_r - 1ULL);
          if(slot_r >= engine->article_count || slot_l == slot_r) continue;
          const ArticleRecord *b = &engine->articles[slot_r];

          const char *witness = match_article_witness(a, b, pivot_dimension);
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
    }
  }
  return found;
}

size_t engine_categorical_pushout(const OyezEngine *engine,
                                  const char *left_region,
                                  const char *right_region,
                                  const char *pivot_dimension,
                                  PushoutAmalgam *out_amalgams,
                                  size_t max_amalgams) {
  if(!engine || !left_region || !right_region || !out_amalgams || max_amalgams == 0) return 0;
  WorldRegion lr, rr;
  if(world_region_from_slug(left_region, &lr) != 0 ||
     world_region_from_slug(right_region, &rr) != 0) {
    return 0;
  }

  PullbackPair pairs[256];
  size_t np = engine_categorical_pullback(engine, left_region, right_region, pivot_dimension, pairs, 256u);
  if(np == 0) return 0;

  size_t count = 0;
  for(size_t p = 0; p < np; ++p) {
    const PullbackPair *pb = &pairs[p];
    if(!pb->shared_witness) continue;

    /* Find or create the pushout equivalence class [w] in X \sqcup_Z Y */
    PushoutAmalgam *am = NULL;
    for(size_t c = 0; c < count; ++c) {
      if(strcmp(out_amalgams[c].shared_witness, pb->shared_witness) == 0) {
        am = &out_amalgams[c];
        break;
      }
    }
    if(!am) {
      if(count >= max_amalgams) continue;
      am = &out_amalgams[count++];
      memset(am, 0, sizeof(*am));
      snprintf(am->shared_witness, sizeof(am->shared_witness), "%s", pb->shared_witness);
      am->shared_closure_mask = 0xFFFFFFFFu;
    }

    const ArticleRecord *sides[2] = {pb->left, pb->right};
    for(int s = 0; s < 2; ++s) {
      const ArticleRecord *art = sides[s];
      int exists = 0;
      for(size_t m = 0; m < am->member_count; ++m) {
        if(am->member_ids[m] == art->id) {
          exists = 1;
          break;
        }
      }
      if(!exists && am->member_count < 16u) {
        am->member_ids[am->member_count++] = art->id;
        if(art->feed_region == lr) am->left_count++;
        if(art->feed_region == rr) am->right_count++;
      }
      am->coproduct_ontology_mask |= art->direct_ontology_mask;
      am->shared_closure_mask &= art->closure_ontology_mask;
    }
  }

  for(size_t c = 0; c < count; ++c) {
    PushoutAmalgam *am = &out_amalgams[c];
    if(am->member_count == 0) am->shared_closure_mask = 0;
    am->sheafified_ontology_mask = ontology_lawvere_tierney_sheafify(am->coproduct_ontology_mask);
    double spec = ontology_information_specificity(am->coproduct_ontology_mask);
    double balance = (am->left_count > 0 && am->right_count > 0)
                     ? (2.0 * (double)am->left_count * (double)am->right_count) /
                       (double)(am->left_count + am->right_count)
                     : 0.5;
    am->colimit_cocone_score = balance * (1.0 + 0.25 * spec) *
                               (double)popcnt64((uint64_t)am->sheafified_ontology_mask);
  }
  return count;
}

void engine_natural_transformation(const OyezEngine *engine,
                                   WorldRegion source_region,
                                   WorldRegion target_region,
                                   PresheafMorphism *out_morphism) {
  if(!out_morphism) return;
  memset(out_morphism, 0, sizeof(*out_morphism));
  out_morphism->source_region = source_region;
  out_morphism->target_region = target_region;
  if(!engine || (uint32_t)source_region >= WORLD_REGION_COUNT ||
     (uint32_t)target_region >= WORLD_REGION_COUNT) {
    return;
  }

  const BitSet *bs_src = &engine->by_region[source_region];
  const BitSet *bs_dst = &engine->by_region[target_region];
  double f_vec[ONTOLOGY_NODE_COUNT];
  double g_vec[ONTOLOGY_NODE_COUNT];

  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t fc = bitset_intersect_count(bs_src, &engine->by_closure_ontology[c]);
    uint32_t gc = bitset_intersect_count(bs_dst, &engine->by_closure_ontology[c]);
    out_morphism->source_fiber_counts[c] = fc;
    out_morphism->target_fiber_counts[c] = gc;
    f_vec[c] = (double)fc;
    g_vec[c] = (double)gc;
    if(fc > 0) out_morphism->source_support_mask |= (1u << c);
    if(gc > 0) out_morphism->target_support_mask |= (1u << c);
    out_morphism->component_ratios[c] = (fc > 0) ? ((double)gc / (double)fc) : (gc > 0 ? 1.0 : 0.0);
  }

  out_morphism->heyting_truth_mask = ontology_heyting_implication(
    out_morphism->source_support_mask,
    out_morphism->target_support_mask
  );
  out_morphism->dense_sieve_mask = ontology_lawvere_tierney_sheafify(
    out_morphism->source_support_mask & out_morphism->target_support_mask
  );
  out_morphism->coend_pairing = ontology_profunctor_coend(f_vec, g_vec);

  /* Naturality Square Defect:
   * For every covering morphism u -> v in C, check commutativity of
   *   F(v) --eta_v--> G(v)
   *    |               |
   *    v               v
   *   F(u) --eta_u--> G(u)
   * normalized by total presheaf mass. */
  double defect = 0.0;
  uint32_t edge_cnt = 0;
  for(uint32_t u = 1u; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t parents = ontology_node((OntologyNodeId)u)->parents_mask;
    while(parents) {
      uint32_t v = ctz64((uint64_t)parents);
      parents &= (parents - 1u);
      if(v == 0) continue;
      double diff = fabs(out_morphism->component_ratios[u] - out_morphism->component_ratios[v]);
      defect += diff / (1.0 + diff);
      edge_cnt++;
    }
  }
  out_morphism->naturality_defect = edge_cnt > 0 ? (defect / (double)edge_cnt) : 0.0;
  out_morphism->lawvere_transport_cost = ontology_lawvere_distance(
    out_morphism->source_support_mask,
    out_morphism->target_support_mask
  );
  out_morphism->reverse_transport_cost = ontology_lawvere_distance(
    out_morphism->target_support_mask,
    out_morphism->source_support_mask
  );
}

static const char *find_cartesian_transport_witness(const ArticleRecord *sub_a,
                                                    const ArticleRecord *sup_a) {
  static const char *const k_pivots[] = {
    "organization",
    "ai_model",
    "asset",
    "cve",
    "fiscal_event",
    "mentioned_country",
    "ai_infra",
    "sport",
    "news_type"
  };
  for(size_t i = 0; i < sizeof(k_pivots) / sizeof(k_pivots[0]); ++i) {
    const char *w = match_article_witness(sub_a, sup_a, k_pivots[i]);
    if(w && w[0] != '\0') return w;
  }
  return NULL;
}

size_t engine_grothendieck_fibration(const OyezEngine *engine,
                                     const char *base_category,
                                     CartesianLift *out_lifts,
                                     size_t max_lifts) {
  if(!engine || !out_lifts || max_lifts == 0 || engine->article_count == 0) return 0;

  OntologyNodeId root_node = ONTO_ROOT;
  if(base_category && base_category[0] != '\0') {
    if(ontology_from_slug(base_category, &root_node) != 0) {
      root_node = ONTO_ROOT;
    }
  }

  uint32_t sieve_mask = ontology_downward_closure(1u << root_node);
  /* If root_node is a leaf with no strict children, include its immediate parents so covering morphisms u -> v exist */
  if(popcnt64((uint64_t)sieve_mask) <= 1u) {
    sieve_mask |= ontology_upward_closure(1u << root_node);
  }

  size_t found = 0;
  for(uint32_t u = 1u; u < ONTOLOGY_NODE_COUNT && found < max_lifts; ++u) {
    if(!(sieve_mask & (1u << u))) continue;
    uint32_t parents = ontology_node((OntologyNodeId)u)->parents_mask;
    while(parents && found < max_lifts) {
      uint32_t v = ctz64((uint64_t)parents);
      parents &= (parents - 1u);
      if(v == 0u || v == u) continue;

      const BitSet *bs_u = &engine->by_direct_ontology[u];
      const BitSet *bs_v = &engine->by_closure_ontology[v];
      if(bitset_popcount(bs_u) == 0) bs_u = &engine->by_closure_ontology[u];

      size_t edge_lifts = 0;
      for(size_t wu = 0; wu < bs_u->word_cap && found < max_lifts && edge_lifts < 4u; ++wu) {
        uint64_t word_u = bs_u->words[wu];
        while(word_u && found < max_lifts && edge_lifts < 4u) {
          uint32_t bit_u = ctz64(word_u);
          uint32_t slot_u = (uint32_t)(wu * 64u + bit_u);
          word_u &= (word_u - 1ULL);
          if(slot_u >= engine->article_count) continue;
          const ArticleRecord *a_u = &engine->articles[slot_u];

          for(size_t wv = 0; wv < bs_v->word_cap && found < max_lifts && edge_lifts < 4u; ++wv) {
            uint64_t word_v = bs_v->words[wv];
            while(word_v && found < max_lifts && edge_lifts < 4u) {
              uint32_t bit_v = ctz64(word_v);
              uint32_t slot_v = (uint32_t)(wv * 64u + bit_v);
              word_v &= (word_v - 1ULL);
              if(slot_v >= engine->article_count || slot_v == slot_u) continue;
              const ArticleRecord *a_v = &engine->articles[slot_v];

              /* Prefer genuine strict lifts where a_v sits in the broader fiber v outside direct u */
              if((a_v->direct_ontology_mask & (1u << u)) != 0u &&
                 bitset_popcount(&engine->by_direct_ontology[v]) > 0) {
                continue;
              }

              const char *witness = find_cartesian_transport_witness(a_u, a_v);
              if(!witness) continue;

              uint32_t hamming = simhash64_distance(a_u->simhash64, a_v->simhash64);
              double geodesic = ontology_lawvere_distance(1u << u, 1u << v);
              double spec_u = ontology_information_specificity(a_u->direct_ontology_mask);
              double fidelity = (1.0 - ((double)hamming / 64.0)) *
                                (1.0 + 0.20 * spec_u) /
                                (1.0 + 0.15 * geodesic);

              CartesianLift *lift = &out_lifts[found++];
              lift->sub_node = (OntologyNodeId)u;
              lift->sup_node = (OntologyNodeId)v;
              lift->sub_article = a_u;
              lift->sup_article = a_v;
              snprintf(lift->transport_witness, sizeof(lift->transport_witness), "%s", witness);
              lift->hamming_distance = hamming;
              lift->lawvere_geodesic = geodesic;
              lift->cartesian_fidelity = fidelity;
              edge_lifts++;
            }
          }
        }
      }
    }
  }

  /* Sort Cartesian lifts in descending order of cartesian_fidelity */
  for(size_t i = 0; i < found; ++i) {
    for(size_t j = i + 1u; j < found; ++j) {
      if(out_lifts[j].cartesian_fidelity > out_lifts[i].cartesian_fidelity) {
        CartesianLift tmp = out_lifts[i];
        out_lifts[i] = out_lifts[j];
        out_lifts[j] = tmp;
      }
    }
  }
  return found;
}

void engine_temporal_sheaf(const OyezEngine *engine,
                           TemporalSheafState *out_temporal) {
  if(!out_temporal) return;
  memset(out_temporal, 0, sizeof(*out_temporal));
  if(!engine || engine->article_count == 0) return;

  size_t n = engine->article_count;
  for(size_t i = 0; i < n; ++i) {
    size_t stalk = (i * TEMPORAL_STALK_COUNT) / n;
    if(stalk >= TEMPORAL_STALK_COUNT) stalk = TEMPORAL_STALK_COUNT - 1u;
    uint32_t mask = engine->articles[i].closure_ontology_mask;
    while(mask) {
      uint32_t c = ctz64((uint64_t)mask);
      mask &= (mask - 1u);
      if(c < ONTOLOGY_NODE_COUNT) {
        out_temporal->stalk_presheaf[stalk][c] += 1.0;
      }
    }
  }

  /* Compute Temporal Sheaf Connection Laplacian ||F_{t+1} - Lan_Y(F_t)||_2 across stalks */
  double conn_sq = 0.0;
  double regime_energy = 0.0;
  for(int t = 0; t < TEMPORAL_STALK_COUNT - 1; ++t) {
    double transported[ONTOLOGY_NODE_COUNT];
    ontology_yoneda_kan_transform(out_temporal->stalk_presheaf[t], transported);
    for(uint32_t c = 1u; c < ONTOLOGY_NODE_COUNT; ++c) {
      double diff = out_temporal->stalk_presheaf[t + 1][c] - transported[c];
      conn_sq += diff * diff;
      double step_v = out_temporal->stalk_presheaf[t + 1][c] - out_temporal->stalk_presheaf[t][c];
      regime_energy += step_v * step_v * ontology_node_magnitude_weight((OntologyNodeId)c);
    }
  }
  out_temporal->connection_laplacian_norm = sqrt(conn_sq);
  out_temporal->regime_shift_energy = sqrt(regime_energy);

  double best_vel = -1e18;
  double best_acc = -1e18;
  OntologyNodeId best_v_node = ONTO_AI;
  OntologyNodeId best_a_node = ONTO_AI;

  for(uint32_t c = 1u; c < ONTOLOGY_NODE_COUNT; ++c) {
    double f0 = out_temporal->stalk_presheaf[0][c];
    double f1 = out_temporal->stalk_presheaf[1][c];
    double f2 = out_temporal->stalk_presheaf[2][c];
    double f3 = out_temporal->stalk_presheaf[3][c];
    double vel = (f3 - f0) / 3.0;
    double acc = (f3 - 2.0 * f2 + f1) - (f2 - 2.0 * f1 + f0);
    out_temporal->velocity[c] = vel;
    out_temporal->acceleration[c] = acc;

    /* Weight by categorical information specificity so leaf domains surface over broad umbrella nodes */
    double spec = ontology_information_specificity(1u << c);
    if(vel * spec > best_vel) {
      best_vel = vel * spec;
      best_v_node = (OntologyNodeId)c;
    }
    if(acc * spec > best_acc) {
      best_acc = acc * spec;
      best_a_node = (OntologyNodeId)c;
    }
  }
  out_temporal->fastest_emerging_node = best_v_node;
  out_temporal->highest_acceleration_node = best_a_node;
}

void engine_topos_state(const OyezEngine *engine,
                        WorldRegion source_region,
                        WorldRegion target_region,
                        OntologyNodeId prop_a,
                        OntologyNodeId prop_b,
                        ToposState *out_topos) {
  if(!out_topos) return;
  memset(out_topos, 0, sizeof(*out_topos));
  out_topos->source_region = (uint32_t)source_region < WORLD_REGION_COUNT ? source_region : REGION_EAST_ASIA;
  out_topos->target_region = (uint32_t)target_region < WORLD_REGION_COUNT ? target_region : REGION_EUROPE;
  out_topos->prop_a = (uint32_t)prop_a < ONTOLOGY_NODE_COUNT ? prop_a : ONTO_AI;
  out_topos->prop_b = (uint32_t)prop_b < ONTOLOGY_NODE_COUNT ? prop_b : ONTO_TECHNOLOGY;

  uint32_t mask_a = 1u << out_topos->prop_a;
  uint32_t mask_b = 1u << out_topos->prop_b;
  out_topos->ideal_a = ontology_downward_closure(mask_a);
  out_topos->ideal_b = ontology_downward_closure(mask_b);
  out_topos->heyting_implication_ab = ontology_heyting_implication(mask_a, mask_b);
  out_topos->heyting_negation_a = ontology_heyting_negation(mask_a);
  out_topos->lawvere_tierney_sheaf_a = ontology_lawvere_tierney_sheafify(mask_a);
  out_topos->coheyting_subtraction_ab = ontology_coheyting_subtraction(mask_a, mask_b);
  out_topos->coheyting_supplement_a = ontology_coheyting_negation(mask_a);
  out_topos->lawvere_boundary_a = ontology_coheyting_boundary(mask_a);
  out_topos->lawvere_boundary_b = ontology_coheyting_boundary(mask_b);

  uint32_t meet_ab = out_topos->ideal_a & out_topos->ideal_b;
  out_topos->lawvere_boundary_meet_ab = ontology_coheyting_boundary(meet_ab);
  uint32_t leibniz_rhs = (out_topos->lawvere_boundary_a & out_topos->ideal_b) |
                         (out_topos->ideal_a & out_topos->lawvere_boundary_b);
  out_topos->leibniz_rule_holds = (out_topos->lawvere_boundary_meet_ab == leibniz_rhs) ? 1 : 0;
  out_topos->universal_quantifier_ab = ontology_universal_quantifier(out_topos->ideal_a | out_topos->ideal_b);
  ontology_subobject_classifier_omega(mask_a, out_topos->omega_truth_a);

  double f_src[ONTOLOGY_NODE_COUNT];
  double f_dst[ONTOLOGY_NODE_COUNT];
  double f_dual[ONTOLOGY_NODE_COUNT];
  const double k_scalar = 100.0;

  const int has_src_reg = (engine && engine->article_count > 0 &&
                           bitset_popcount(&engine->by_region[out_topos->source_region]) > 0);
  const int has_dst_reg = (engine && engine->article_count > 0 &&
                           bitset_popcount(&engine->by_region[out_topos->target_region]) > 0);

  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double sc = has_src_reg
                    ? (double)bitset_intersect_count(&engine->by_region[out_topos->source_region],
                                                     &engine->by_closure_ontology[c])
                    : (double)popcnt64((uint64_t)ontology_node((OntologyNodeId)c)->downward_mask);
    double dc = has_dst_reg
                    ? (double)bitset_intersect_count(&engine->by_region[out_topos->target_region],
                                                     &engine->by_closure_ontology[c])
                    : (double)popcnt64((uint64_t)ontology_node((OntologyNodeId)c)->upward_mask);
    f_src[c] = sc;
    f_dst[c] = dc;
    f_dual[c] = k_scalar - dc;

    double diff = fabs(sc - dc);
    double scale = (sc + dc) > 1.0 ? (sc + dc) : 1.0;
    if(diff <= 0.35 * scale) {
      out_topos->equalizer_mask |= (1u << c);
    }
    out_topos->coequalizer_quotient[c] = 0.5 * (sc + dc) + 0.5 * diff * ontology_node_magnitude_weight((OntologyNodeId)c);
  }
  /* Close equalizer subobject downward so Eq(F_src, F_dst) is a genuine subpresheaf sieve */
  out_topos->equalizer_mask = ontology_universal_quantifier(out_topos->equalizer_mask);

  ontology_isbell_conjugate_covariant(f_src, out_topos->isbell_covariant_star);
  ontology_isbell_conjugate_contravariant(out_topos->isbell_covariant_star, out_topos->isbell_double_hull);
  out_topos->isbell_envelope_defect = ontology_isbell_envelope_defect(f_src);
  out_topos->profunctor_end = ontology_profunctor_end(f_src, f_dst);
  out_topos->profunctor_coend = ontology_profunctor_coend(f_src, f_dst);
  out_topos->weighted_colimit = ontology_weighted_colimit(f_src, f_dst);
  out_topos->weighted_limit = ontology_weighted_limit(f_src, f_dst);
  double lim_dual = ontology_weighted_limit(f_src, f_dual);
  out_topos->tensor_hom_adjunction_defect = fabs((k_scalar - out_topos->weighted_colimit) - lim_dual);

  /* Day Convolution, Day Internal Hom, and Day Tensor-Hom Adjunction Defect */
  ontology_day_convolution(f_src, f_dst, out_topos->day_convolution_fg);
  ontology_day_internal_hom(f_dst, out_topos->coequalizer_quotient, out_topos->day_internal_hom_gh);
  out_topos->day_adjunction_defect = ontology_day_adjunction_defect(f_src, f_dst, out_topos->coequalizer_quotient);

  /* Quillen Model Category Factorization A ~-> M ->> (A v B) */
  ontology_quillen_factorize(mask_a, mask_b,
                             &out_topos->quillen_acyclic_cofibration_m,
                             &out_topos->quillen_fibration_target,
                             &out_topos->quillen_weak_equivalence_holds);

  /* Spivak Polynomial Endofunctor P_C(X) & Dependent Co-Design Lens */
  ontology_polynomial_functor_eval(f_src, out_topos->polynomial_functor_px, out_topos->polynomial_codesign_lens);

  /* Spectral Heat Kernel Semigroup H_t = exp(-t L_0) */
  ontology_heat_kernel_propagate(f_src, 0.25, out_topos->heat_diffused_src);

  /* Entropic Sinkhorn-Knopp Optimal Transport, Debiased Sinkhorn Divergence & Fisher-Rao Geodesic */
  ontology_sinkhorn_optimal_transport(f_src, f_dst, 0.75, &out_topos->sinkhorn_ot);

  /* Grothendieck-Leray Spectral Sequence (E_r^{p,q}, d_r) of Rank-Filtered Nerve F_p N(C) */
  ontology_spectral_sequence(f_src, &out_topos->spectral_seq);

  /* Simplicial Gauss-Bonnet-Chern, Forman-Ricci, Ollivier-Ricci Curvature & Discrete Ricci Flow */
  ontology_ricci_curvature(f_src, &out_topos->ricci_curv);

  /* Ihara-Bass Graph Zeta Function zeta_C(u), Karp's Tropical Eigenvalue & Tropical Polytope Hull */
  ontology_ihara_zeta_and_tropical(f_src, 0.15, &out_topos->zeta_tropical);

  /* Commutative Rota-Baxter Algebra (A, R) of Weight theta = -1 & Atkinson Birkhoff Factorization */
  ontology_rota_baxter_algebra(f_src, f_dst, &out_topos->rota_baxter);
}

size_t engine_operad_compose(const OyezEngine *engine,
                             const char *region_a,
                             const char *region_b,
                             const char *region_c,
                             OperadChain *out_chains,
                             size_t max_chains) {
  if(!engine || !out_chains || max_chains == 0 || engine->article_count < 3u) return 0;

  WorldRegion ra = REGION_EAST_ASIA, rb = REGION_NORTH_AMERICA, rc = REGION_EUROPE;
  if(region_a && region_a[0]) world_region_from_slug(region_a, &ra);
  if(region_b && region_b[0]) world_region_from_slug(region_b, &rb);
  if(region_c && region_c[0]) world_region_from_slug(region_c, &rc);

  const BitSet *bs_a = &engine->by_region[ra];
  const BitSet *bs_b = &engine->by_region[rb];
  const BitSet *bs_c = &engine->by_region[rc];
  if(bitset_popcount(bs_a) == 0) bs_a = &engine->all_articles;
  if(bitset_popcount(bs_b) == 0) bs_b = &engine->all_articles;
  if(bitset_popcount(bs_c) == 0) bs_c = &engine->all_articles;

  size_t found = 0;
  for(size_t wb = 0; wb < bs_b->word_cap && found < max_chains; ++wb) {
    uint64_t word_b = bs_b->words[wb];
    while(word_b && found < max_chains) {
      uint32_t bit_b = ctz64(word_b);
      uint32_t slot_b = (uint32_t)(wb * 64u + bit_b);
      word_b &= (word_b - 1ULL);
      if(slot_b >= engine->article_count) continue;
      const ArticleRecord *art_b = &engine->articles[slot_b];

      const ArticleRecord *best_a = NULL;
      const char *best_wab = NULL;
      for(size_t wa = 0; wa < bs_a->word_cap && !best_a; ++wa) {
        uint64_t word_a = bs_a->words[wa];
        while(word_a) {
          uint32_t bit_a = ctz64(word_a);
          uint32_t slot_a = (uint32_t)(wa * 64u + bit_a);
          word_a &= (word_a - 1ULL);
          if(slot_a >= engine->article_count || slot_a == slot_b) continue;
          const ArticleRecord *cand_a = &engine->articles[slot_a];
          const char *w = find_cartesian_transport_witness(cand_a, art_b);
          if(w) {
            best_a = cand_a;
            best_wab = w;
            break;
          }
        }
      }
      if(!best_a || !best_wab) continue;

      for(size_t wc = 0; wc < bs_c->word_cap && found < max_chains; ++wc) {
        uint64_t word_c = bs_c->words[wc];
        while(word_c && found < max_chains) {
          uint32_t bit_c = ctz64(word_c);
          uint32_t slot_c = (uint32_t)(wc * 64u + bit_c);
          word_c &= (word_c - 1ULL);
          if(slot_c >= engine->article_count || slot_c == slot_b || slot_c == best_a->slot) continue;
          const ArticleRecord *art_c = &engine->articles[slot_c];
          const char *w_bc = find_cartesian_transport_witness(art_b, art_c);
          if(!w_bc) continue;

          OperadChain *ch = &out_chains[found++];
          ch->stage_a = best_a;
          ch->stage_b = art_b;
          ch->stage_c = art_c;
          snprintf(ch->witness_ab, sizeof(ch->witness_ab), "%s", best_wab);
          snprintf(ch->witness_bc, sizeof(ch->witness_bc), "%s", w_bc);
          ch->composite_ontology_mask = ontology_lawvere_tierney_sheafify(
            best_a->direct_ontology_mask | art_b->direct_ontology_mask | art_c->direct_ontology_mask
          );
          double d_ab = ontology_lawvere_distance(best_a->direct_ontology_mask, art_b->direct_ontology_mask);
          double d_bc = ontology_lawvere_distance(art_b->direct_ontology_mask, art_c->direct_ontology_mask);
          ch->total_lawvere_geodesic = d_ab + d_bc;
          double sim_ab = article_similarity(best_a, art_b);
          double sim_bc = article_similarity(art_b, art_c);
          double spec = ontology_information_specificity(ch->composite_ontology_mask);
          ch->operad_composition_score = (sim_ab + sim_bc) * spec / (1.0 + 0.1 * ch->total_lawvere_geodesic);
          break;
        }
      }
    }
  }

  for(size_t i = 0; i < found; ++i) {
    for(size_t j = i + 1u; j < found; ++j) {
      if(out_chains[j].operad_composition_score > out_chains[i].operad_composition_score) {
        OperadChain tmp = out_chains[i];
        out_chains[i] = out_chains[j];
        out_chains[j] = tmp;
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

  RankedEntity ents[256];
  const BitSet *ent_fibers[256];
  size_t ent_count = 0;

  for(uint32_t c = 1u; c < ONTOLOGY_NODE_COUNT && ent_count < 256u; ++c) {
    uint32_t deg = bitset_popcount(&engine->by_closure_ontology[c]);
    if(deg == 0) continue;
    ent_fibers[ent_count] = &engine->by_closure_ontology[c];
    RankedEntity *e = &ents[ent_count++];
    snprintf(e->id, sizeof(e->id), "cat:%s", ontology_node((OntologyNodeId)c)->slug);
    snprintf(e->label, sizeof(e->label), "%s", ontology_node((OntologyNodeId)c)->slug);
    e->kind = "category";
    e->degree = deg;
    e->score = 1.0;
  }

  for(size_t i = 0; i < 256u && ent_count < 256u; ++i) {
    if(engine->by_mentioned_country[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_mentioned_country[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_mentioned_country[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "geo:%s", engine->by_mentioned_country[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_mentioned_country[i].key);
      e->kind = "country";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_asset[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_asset[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_asset[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "asset:%s", engine->by_asset[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_asset[i].key);
      e->kind = "asset";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_org[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_org[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_org[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "org:%s", engine->by_org[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_org[i].key);
      e->kind = "organization";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_ai_model[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_ai_model[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_ai_model[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "model:%s", engine->by_ai_model[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_ai_model[i].key);
      e->kind = "ai_model";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_ai_infra[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_ai_infra[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_ai_infra[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "infra:%s", engine->by_ai_infra[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_ai_infra[i].key);
      e->kind = "ai_infra";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_hf_repo[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_hf_repo[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_hf_repo[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "hf:%s", engine->by_hf_repo[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_hf_repo[i].key);
      e->kind = "hf_repo";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_sport[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_sport[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_sport[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "sport:%s", engine->by_sport[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_sport[i].key);
      e->kind = "sport";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  for(size_t i = 0; i < 128u && ent_count < 256u; ++i) {
    if(engine->by_weather[i].occupied) {
      uint32_t deg = bitset_popcount(&engine->by_weather[i].bits);
      if(deg == 0) continue;
      ent_fibers[ent_count] = &engine->by_weather[i].bits;
      RankedEntity *e = &ents[ent_count++];
      snprintf(e->id, sizeof(e->id), "weather:%s", engine->by_weather[i].key);
      snprintf(e->label, sizeof(e->label), "%s", engine->by_weather[i].key);
      e->kind = "weather";
      e->degree = deg;
      e->score = 1.0;
    }
  }

  if(ent_count == 0) return 0;
  size_t N_art = engine->article_count;
  double *art_scores = malloc(N_art * sizeof(double));
  uint32_t *art_out_deg = calloc(N_art, sizeof(uint32_t));
  if(!art_scores || !art_out_deg) {
    free(art_scores);
    free(art_out_deg);
    return 0;
  }

  /* Precompute article out-degrees across all active entity BitSet fibers in O(1) per 64-bit word */
  for(size_t e = 0; e < ent_count; ++e) {
    const BitSet *bs = ent_fibers[e];
    for(size_t w = 0; w < bs->word_cap; ++w) {
      uint64_t word = bs->words[w];
      while(word) {
        uint32_t bit = ctz64(word);
        uint32_t slot = (uint32_t)(w * 64u + bit);
        word &= (word - 1ULL);
        if(slot < N_art) art_out_deg[slot]++;
      }
    }
  }

  if(iterations == 0 || iterations > 200u) iterations = 12u;
  if(!(damping > 0.0 && damping < 1.0)) damping = 0.85;

  double *inv_art_deg = malloc(N_art * sizeof(double));
  if(!inv_art_deg) {
    free(art_scores);
    free(art_out_deg);
    return 0;
  }
  for(size_t i = 0; i < N_art; ++i) {
    art_scores[i] = 1.0 / (double)N_art;
    inv_art_deg[i] = art_out_deg[i] > 0 ? (damping / (double)art_out_deg[i]) : 0.0;
  }
  for(size_t e = 0; e < ent_count; ++e) ents[e].score = 1.0 / (double)ent_count;

  double base_ent = (1.0 - damping) / (double)ent_count;
  double base_art = (1.0 - damping) / (double)N_art;

  for(uint32_t iter = 0; iter < iterations; ++iter) {
    /* Step 1: Articles -> Entities via hardware ctz64 over each entity's BitSet fiber */
    for(size_t e = 0; e < ent_count; ++e) {
      double acc = base_ent;
      const BitSet *bs = ent_fibers[e];
      for(size_t w = 0; w < bs->word_cap; ++w) {
        uint64_t word = bs->words[w];
        while(word) {
          uint32_t bit = ctz64(word);
          uint32_t slot = (uint32_t)(w * 64u + bit);
          word &= (word - 1ULL);
          if(slot < N_art) {
            acc += art_scores[slot] * inv_art_deg[slot];
          }
        }
      }
      ents[e].score = acc;
    }

    /* Step 2: Entities -> Articles via hardware ctz64 over each entity's BitSet fiber */
    for(size_t i = 0; i < N_art; ++i) art_scores[i] = base_art;
    for(size_t e = 0; e < ent_count; ++e) {
      if(ents[e].degree == 0) continue;
      double ent_share = damping * (ents[e].score / (double)ents[e].degree);
      const BitSet *bs = ent_fibers[e];
      for(size_t w = 0; w < bs->word_cap; ++w) {
        uint64_t word = bs->words[w];
        while(word) {
          uint32_t bit = ctz64(word);
          uint32_t slot = (uint32_t)(w * 64u + bit);
          word &= (word - 1ULL);
          if(slot < N_art) {
            art_scores[slot] += ent_share;
          }
        }
      }
    }
  }

  free(art_out_deg);
  free(inv_art_deg);

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

  /* Consolidated One-Machine Extension Block ("OYEZAU01"):
   * Persists per-feed UCB1 bandit telemetry, automation scheduler counters,
   * and all custom/default watchpoint rules inside the single atomic snapshot. */
  buffer_append(&buf, "OYEZAU01", 8u);
  for(size_t i = 0; i < engine->feed_count; ++i) {
    const FeedRecord *f = &engine->feeds[i];
    wire_append_u32(&buf, f->fetch_count);
    wire_append_u32(&buf, f->error_count);
    wire_append_u64(&buf, f->last_fetched_ms);
  }
  wire_append_u64(&buf, engine->total_bandit_pulls);
  wire_append_u32(&buf, engine->auto_poll_interval_sec);
  wire_append_u64(&buf, engine->last_auto_cycle_ms);
  wire_append_u64(&buf, engine->auto_cycles_completed);
  wire_append_u32(&buf, engine->next_rule_id);
  wire_append_u32(&buf, (uint32_t)engine->automation_rule_count);
  for(size_t i = 0; i < engine->automation_rule_count; ++i) {
    const AutomationRule *r = &engine->automation_rules[i];
    wire_append_u32(&buf, r->id);
    wire_append_cstr(&buf, r->name);
    wire_append_cstr(&buf, r->query);
    wire_append_cstr(&buf, r->category);
    wire_append_cstr(&buf, r->region);
    wire_append_cstr(&buf, r->weather);
    wire_append_u32(&buf, (uint32_t)r->min_clearance);
    wire_append_u32(&buf, (uint32_t)r->fiscal_stance);
    wire_append_u32(&buf, (uint32_t)r->min_ai_tier);
    wire_append_u32(&buf, (uint32_t)r->min_weather_severity);
    uint64_t score_bits = 0;
    memcpy(&score_bits, &r->min_score, sizeof(score_bits));
    wire_append_u64(&buf, score_bits);
    wire_append_u32(&buf, (uint32_t)r->action);
    wire_append_cstr(&buf, r->target);
    wire_append_u32(&buf, r->hit_count);
    wire_append_u64(&buf, r->last_triggered_ms);
    wire_append_u32(&buf, (uint32_t)(r->enabled ? 1u : 0u));
  }

  if(buf.truncated || !buf.data) {
    buffer_free(&buf);
    return -1;
  }
  uint32_t crc = checksum_crc32(buf.data, buf.len);
  wire_append_u32(&buf, crc);
  if(buf.truncated || !buf.data) {
    buffer_free(&buf);
    return -1;
  }

  char tmp_path[1024];
  int tn = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  const char *write_path = (tn > 0 && (size_t)tn < sizeof(tmp_path)) ? tmp_path : path;

  FILE *fp = fopen(write_path, "wb");
  if(!fp) {
    buffer_free(&buf);
    return -1;
  }
  size_t written = fwrite(buf.data, 1u, buf.len, fp);
  int flush_rc = fflush(fp);
  fclose(fp);
  size_t expected = buf.len;
  buffer_free(&buf);
  if(written != expected || flush_rc != 0) {
    if(write_path == tmp_path) remove(tmp_path);
    return -1;
  }
  if(write_path == tmp_path && rename(tmp_path, path) != 0) {
    remove(tmp_path);
    return -1;
  }
  return 0;
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
  if(sz < 20 || sz > 256L * 1024L * 1024L || fseek(fp, 0, SEEK_SET) != 0) {
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

  OyezEngine staged;
  engine_init(&staged);

  size_t off = 8u;
  uint32_t fcount = 0;
  uint32_t acount = 0;
  if(!wire_read_u32(bytes, payload_len, &off, &fcount) ||
     !wire_read_u32(bytes, payload_len, &off, &acount) ||
     fcount > 100000u || acount > 1000000u) {
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
    if(!title || !furl || !surl || !cat || !country || !subreg || !lang || !tier || !focus ||
       fid == 0u || fid == UINT32_MAX) {
      free(title); free(furl); free(surl); free(cat);
      free(country); free(subreg); free(lang); free(tier); free(focus);
      goto fail;
    }
    if(reg >= WORLD_REGION_COUNT) reg = (uint32_t)REGION_GLOBAL;
    CatalogEntry ce = {title, furl, surl, cat, (WorldRegion)reg, country, subreg, lang, tier, focus};
    uint32_t actual_id = 0;
    size_t prior_count = staged.feed_count;
    int up_rc = engine_upsert_feed(&staged, &ce, &actual_id);
    free(title); free(furl); free(surl); free(cat);
    free(country); free(subreg); free(lang); free(tier); free(focus);
    if(up_rc != 0 || staged.feed_count != prior_count + 1u) goto fail;
    for(size_t j = 0; j < prior_count; ++j) {
      if(staged.feeds[j].id == fid) goto fail;
    }
    staged.feeds[staged.feed_count - 1u].id = fid;
    if(fid >= staged.next_feed_id) staged.next_feed_id = fid + 1u;
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
    int inserted = 0;
    int ingest_rc = engine_ingest_article(&staged, fid, guid, title, url, author, summary,
                                          chtml, pub_at, pub_ms, img, NULL, 0, NULL, &inserted);
    free(guid); free(title); free(url); free(author); free(summary); free(chtml); free(pub_at); free(img);
    if(ingest_rc != 0 || !inserted) goto fail;
  }

  if(off + 8u <= payload_len && memcmp(bytes + off, "OYEZAU01", 8u) == 0) {
    off += 8u;
    for(uint32_t i = 0; i < fcount; ++i) {
      uint32_t f_cnt = 0;
      uint32_t e_cnt = 0;
      uint64_t last_ms = 0;
      if(!wire_read_u32(bytes, payload_len, &off, &f_cnt) ||
         !wire_read_u32(bytes, payload_len, &off, &e_cnt) ||
         !wire_read_u64(bytes, payload_len, &off, &last_ms)) {
        goto fail;
      }
      staged.feeds[i].fetch_count = f_cnt;
      staged.feeds[i].error_count = e_cnt;
      staged.feeds[i].last_fetched_ms = last_ms;
    }
    uint64_t b_pulls = 0;
    uint32_t poll_sec = 0;
    uint64_t last_cyc_ms = 0;
    uint64_t cyc_done = 0;
    uint32_t next_rid = 1u;
    uint32_t rcount = 0;
    if(!wire_read_u64(bytes, payload_len, &off, &b_pulls) ||
       !wire_read_u32(bytes, payload_len, &off, &poll_sec) ||
       !wire_read_u64(bytes, payload_len, &off, &last_cyc_ms) ||
       !wire_read_u64(bytes, payload_len, &off, &cyc_done) ||
       !wire_read_u32(bytes, payload_len, &off, &next_rid) ||
       !wire_read_u32(bytes, payload_len, &off, &rcount) ||
       rcount > MAX_AUTOMATION_RULES) {
      goto fail;
    }
    staged.total_bandit_pulls = b_pulls;
    staged.auto_poll_interval_sec = poll_sec;
    staged.last_auto_cycle_ms = last_cyc_ms;
    staged.auto_cycles_completed = cyc_done;
    staged.next_rule_id = next_rid > 0 ? next_rid : 1u;
    staged.automation_rule_count = 0;
    uint32_t saved_hits[MAX_AUTOMATION_RULES] = {0};
    uint64_t saved_last_ms[MAX_AUTOMATION_RULES] = {0};

    for(uint32_t i = 0; i < rcount; ++i) {
      uint32_t rid = 0;
      if(!wire_read_u32(bytes, payload_len, &off, &rid)) goto fail;
      char *rname = wire_read_cstr(bytes, payload_len, &off);
      char *rquery = wire_read_cstr(bytes, payload_len, &off);
      char *rcat = wire_read_cstr(bytes, payload_len, &off);
      char *rreg = wire_read_cstr(bytes, payload_len, &off);
      char *rwx = wire_read_cstr(bytes, payload_len, &off);
      uint32_t rclr = 0, rfisc = 0, rai = 0, rwxsev = 0, ract = 0, rhits = 0, ren = 0;
      uint64_t rscore_bits = 0, rlast = 0;
      if(!rname || !rquery || !rcat || !rreg || !rwx ||
         !wire_read_u32(bytes, payload_len, &off, &rclr) ||
         !wire_read_u32(bytes, payload_len, &off, &rfisc) ||
         !wire_read_u32(bytes, payload_len, &off, &rai) ||
         !wire_read_u32(bytes, payload_len, &off, &rwxsev) ||
         !wire_read_u64(bytes, payload_len, &off, &rscore_bits)) {
        free(rname); free(rquery); free(rcat); free(rreg); free(rwx);
        goto fail;
      }
      if(!wire_read_u32(bytes, payload_len, &off, &ract)) {
        free(rname); free(rquery); free(rcat); free(rreg); free(rwx);
        goto fail;
      }
      char *rtarg = wire_read_cstr(bytes, payload_len, &off);
      if(!rtarg ||
         !wire_read_u32(bytes, payload_len, &off, &rhits) ||
         !wire_read_u64(bytes, payload_len, &off, &rlast) ||
         !wire_read_u32(bytes, payload_len, &off, &ren)) {
        free(rname); free(rquery); free(rcat); free(rreg); free(rwx); free(rtarg);
        goto fail;
      }
      AutomationRule *dst = &staged.automation_rules[staged.automation_rule_count++];
      memset(dst, 0, sizeof(*dst));
      dst->id = rid > 0 ? rid : (i + 1u);
      if(dst->id >= staged.next_rule_id) staged.next_rule_id = dst->id + 1u;
      strncpy(dst->name, rname, sizeof(dst->name) - 1u);
      strncpy(dst->query, rquery, sizeof(dst->query) - 1u);
      strncpy(dst->category, rcat, sizeof(dst->category) - 1u);
      strncpy(dst->region, rreg, sizeof(dst->region) - 1u);
      strncpy(dst->weather, rwx, sizeof(dst->weather) - 1u);
      dst->min_clearance = rclr < CLEARANCE_LEVEL_COUNT ? (ClearanceLevel)rclr : CLEARANCE_PUBLIC;
      dst->fiscal_stance = rfisc < FISCAL_STANCE_COUNT ? (FiscalStance)rfisc : FISCAL_NEUTRAL;
      dst->min_ai_tier = rai < AI_TIER_COUNT ? (AiComputeTier)rai : AI_TIER_NONE;
      dst->min_weather_severity = rwxsev < WEATHER_SEVERITY_COUNT ? (WeatherSeverity)rwxsev : WEATHER_CALM;
      memcpy(&dst->min_score, &rscore_bits, sizeof(dst->min_score));
      dst->action = ract <= (uint32_t)AUTO_ACTION_SNAPSHOT ? (AutomationActionType)ract : AUTO_ACTION_ALERT_RING;
      strncpy(dst->target, rtarg, sizeof(dst->target) - 1u);
      dst->hit_count = rhits;
      dst->last_triggered_ms = rlast;
      dst->enabled = ren ? 1 : 0;
      saved_hits[i] = rhits;
      saved_last_ms[i] = rlast;
      free(rname); free(rquery); free(rcat); free(rreg); free(rwx); free(rtarg);
    }
    /* Re-populate the in-memory alert ring buffer against restored watchpoint rules */
    staged.automation_alert_count = 0;
    staged.automation_alert_head = 0;
    staged.next_alert_id = 1u;
    (void)engine_evaluate_all_automation_rules(&staged);
    for(uint32_t i = 0; i < rcount; ++i) {
      if(saved_hits[i] > staged.automation_rules[i].hit_count) {
        staged.automation_rules[i].hit_count = saved_hits[i];
      }
      if(saved_last_ms[i] > 0) {
        staged.automation_rules[i].last_triggered_ms = saved_last_ms[i];
      }
    }
  }

  if(off != payload_len) goto fail;
  free(bytes);
  pthread_mutex_destroy(&staged.cache_lock);
  pthread_rwlock_destroy(&staged.rwlock);
  engine_free(engine);
  memcpy(engine, &staged, offsetof(OyezEngine, rwlock));
  pthread_rwlock_init(&engine->rwlock, NULL);
  pthread_mutex_init(&engine->cache_lock, NULL);
  return 0;

fail:
  engine_free(&staged);
  free(bytes);
  return -1;
}

int engine_upsert_automation_rule(OyezEngine *engine, const AutomationRule *rule, uint32_t *out_rule_id) {
  if(!engine || !rule || !rule->name[0]) return -1;
  for(size_t i = 0; i < engine->automation_rule_count; ++i) {
    if(strcmp(engine->automation_rules[i].name, rule->name) == 0 ||
       (rule->id > 0 && engine->automation_rules[i].id == rule->id)) {
      uint32_t keep_id = engine->automation_rules[i].id;
      uint32_t keep_hits = engine->automation_rules[i].hit_count;
      uint64_t keep_last = engine->automation_rules[i].last_triggered_ms;
      engine->automation_rules[i] = *rule;
      engine->automation_rules[i].id = keep_id;
      engine->automation_rules[i].hit_count = keep_hits;
      engine->automation_rules[i].last_triggered_ms = keep_last;
      if(out_rule_id) *out_rule_id = keep_id;
      return 0;
    }
  }
  if(engine->automation_rule_count >= MAX_AUTOMATION_RULES) return -1;
  AutomationRule *dst = &engine->automation_rules[engine->automation_rule_count++];
  *dst = *rule;
  dst->id = engine->next_rule_id++;
  dst->hit_count = 0;
  dst->last_triggered_ms = 0;
  if(out_rule_id) *out_rule_id = dst->id;
  return 0;
}

int engine_delete_automation_rule(OyezEngine *engine, uint32_t rule_id) {
  if(!engine || rule_id == 0) return -1;
  for(size_t i = 0; i < engine->automation_rule_count; ++i) {
    if(engine->automation_rules[i].id == rule_id) {
      for(size_t j = i + 1u; j < engine->automation_rule_count; ++j) {
        engine->automation_rules[j - 1u] = engine->automation_rules[j];
      }
      engine->automation_rule_count--;
      return 0;
    }
  }
  return -1;
}

size_t engine_evaluate_all_automation_rules(OyezEngine *engine) {
  if(!engine) return 0;
  size_t total_fired = 0;
  for(size_t i = 0; i < engine->article_count; ++i) {
    total_fired += evaluate_article_automation_rules(engine, &engine->articles[i]);
  }
  return total_fired;
}

static int compare_bandit_desc(const void *a, const void *b) {
  const BanditFeedPriority *pa = (const BanditFeedPriority *)a;
  const BanditFeedPriority *pb = (const BanditFeedPriority *)b;
  if(pb->ucb1_score > pa->ucb1_score) return 1;
  if(pb->ucb1_score < pa->ucb1_score) return -1;
  if(pa->feed_id < pb->feed_id) return -1;
  if(pa->feed_id > pb->feed_id) return 1;
  return 0;
}

size_t engine_bandit_select_feeds(const OyezEngine *engine,
                                  BanditFeedPriority *out_priorities,
                                  size_t max_out) {
  if(!engine || engine->feed_count == 0 || !out_priorities || max_out == 0) return 0;
  BanditFeedPriority *all = malloc(engine->feed_count * sizeof(BanditFeedPriority));
  if(!all) return 0;

  uint64_t total_pulls = engine->total_bandit_pulls;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    total_pulls += engine->feeds[i].fetch_count;
  }
  double ln_t = log(2.0 + (double)total_pulls);
  uint64_t now_ms = wall_time_ms();

  for(size_t i = 0; i < engine->feed_count; ++i) {
    const FeedRecord *f = &engine->feeds[i];
    double n_i = 1.0 + (double)f->fetch_count;
    double emp_yield = (double)f->article_count / n_i;
    /* Auer-Cesa-Bianchi-Fischer UCB1 upper confidence bound */
    double explore = 1.41421356 * sqrt((2.0 * ln_t) / n_i);
    double err_pen = 1.5 * ((double)f->error_count / (n_i + (double)f->error_count));
    double stale = 0.0;
    if(f->last_fetched_ms == 0) {
      stale = 0.85;
    } else if(now_ms > f->last_fetched_ms) {
      double age_min = (double)(now_ms - f->last_fetched_ms) / 60000.0;
      stale = age_min > 120.0 ? 0.75 : (age_min / 160.0);
    }
    /* High-tempo category priority boost (weather/climate, cyber, ai, finance) */
    if(f->category &&
       (strcmp(f->category, "climate") == 0 || strcmp(f->category, "cyber") == 0 ||
        strcmp(f->category, "ai") == 0 || strcmp(f->category, "finance") == 0)) {
      stale += 0.25;
    }
    all[i].feed_id = f->id;
    all[i].title = f->title;
    all[i].category = f->category;
    all[i].empirical_yield = emp_yield;
    all[i].exploration_bonus = explore;
    all[i].staleness_bonus = stale;
    all[i].error_penalty = err_pen;
    all[i].ucb1_score = emp_yield + explore + stale - err_pen;
  }

  qsort(all, engine->feed_count, sizeof(BanditFeedPriority), compare_bandit_desc);
  size_t take = engine->feed_count < max_out ? engine->feed_count : max_out;
  memcpy(out_priorities, all, take * sizeof(BanditFeedPriority));
  free(all);
  return take;
}

static size_t cstr_heap_bytes(const char *s) {
  return s ? (strlen(s) + 1u) : 0u;
}

static size_t country_fiber_array_bytes(const CountryFiber *arr, size_t count) {
  size_t total = 0;
  for(size_t i = 0; i < count; ++i) {
    if(arr[i].occupied && arr[i].bits.words) {
      total += arr[i].bits.word_cap * sizeof(uint64_t);
    }
  }
  return total;
}

void engine_appliance_state(const OyezEngine *engine,
                            EngineApplianceState *out_app) {
  if(!out_app) return;
  memset(out_app, 0, sizeof(*out_app));
  if(!engine) return;

  /* 1. Single-Machine Resident Memory Accounting */
  out_app->struct_base_bytes = sizeof(OyezEngine);
  for(const struct StringArenaBlock *b = engine->arena_head; b != NULL; b = b->next) {
    out_app->arena_block_count++;
    out_app->arena_bytes_used += b->used;
    out_app->arena_bytes_capacity += sizeof(*b) + b->cap;
  }

  size_t feed_str_bytes = 0;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    const FeedRecord *f = &engine->feeds[i];
    feed_str_bytes += cstr_heap_bytes(f->title) + cstr_heap_bytes(f->feed_url) +
                      cstr_heap_bytes(f->site_url) + cstr_heap_bytes(f->category) +
                      cstr_heap_bytes(f->country) + cstr_heap_bytes(f->subregion) +
                      cstr_heap_bytes(f->language) + cstr_heap_bytes(f->source_tier) +
                      cstr_heap_bytes(f->editorial_focus);
  }
  out_app->feed_and_article_table_bytes =
      engine->feed_cap * sizeof(FeedRecord) +
      engine->article_cap * sizeof(ArticleRecord) +
      feed_str_bytes;

  size_t inv_bytes = engine->inv_cap * sizeof(InvertedBucket);
  for(size_t i = 0; i < engine->inv_cap; ++i) {
    if(engine->inv_table && engine->inv_table[i].occupied) {
      inv_bytes += (size_t)engine->inv_table[i].cap *
                   (sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t));
    }
  }
  out_app->inverted_index_bytes = inv_bytes;

  size_t bit_bytes = engine->all_articles.word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < WORLD_REGION_COUNT; ++i) bit_bytes += engine->by_region[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < NEWS_TYPE_COUNT; ++i) bit_bytes += engine->by_news_type[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < CLEARANCE_LEVEL_COUNT; ++i) bit_bytes += engine->by_clearance[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < FISCAL_STANCE_COUNT; ++i) bit_bytes += engine->by_fiscal_stance[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < AI_TIER_COUNT; ++i) bit_bytes += engine->by_ai_tier[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < AI_LICENSE_COUNT; ++i) bit_bytes += engine->by_ai_license[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < WEATHER_SEVERITY_COUNT; ++i) bit_bytes += engine->by_weather_severity[i].word_cap * sizeof(uint64_t);
  for(size_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    bit_bytes += engine->by_direct_ontology[i].word_cap * sizeof(uint64_t);
    bit_bytes += engine->by_closure_ontology[i].word_cap * sizeof(uint64_t);
  }
  bit_bytes += country_fiber_array_bytes(engine->by_source_country, 256u);
  bit_bytes += country_fiber_array_bytes(engine->by_mentioned_country, 256u);
  bit_bytes += country_fiber_array_bytes(engine->by_asset, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_fiscal_event, 64u);
  bit_bytes += country_fiber_array_bytes(engine->by_cve, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_org, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_ai_model, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_ai_infra, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_hf_repo, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_sport, 128u);
  bit_bytes += country_fiber_array_bytes(engine->by_weather, 128u);
  out_app->bitset_fiber_bytes = bit_bytes;

  size_t lsh_bytes = engine->dedup_cap * sizeof(engine->dedup_table[0]);
  for(size_t band = 0; band < 4u; ++band) {
    if(engine->lsh_bands[band]) {
      lsh_bytes += 4096u * sizeof(SimHashBucket);
      for(size_t b = 0; b < 4096u; ++b) {
        lsh_bytes += (size_t)engine->lsh_bands[band][b].cap * sizeof(uint32_t);
      }
    }
  }
  out_app->lsh_and_dedup_bytes = lsh_bytes;

  size_t qcache_bytes = 0;
  size_t qcache_valid = 0;
  for(size_t c = 0; c < 64u; ++c) {
    if(engine->query_cache[c].valid) {
      qcache_valid++;
      qcache_bytes += engine->query_cache[c].count * (sizeof(uint32_t) + sizeof(double));
    }
  }
  out_app->query_cache_bytes = qcache_bytes;
  out_app->query_cache_valid_entries = qcache_valid;
  out_app->index_generation = engine->index_generation;
  out_app->total_resident_bytes =
      out_app->struct_base_bytes +
      out_app->arena_bytes_capacity +
      out_app->feed_and_article_table_bytes +
      out_app->inverted_index_bytes +
      out_app->bitset_fiber_bytes +
      out_app->lsh_and_dedup_bytes +
      out_app->query_cache_bytes;

  /* 2. 12-Theorem Mathematical & Topos Self-Verification */
  double raw_f[ONTOLOGY_NODE_COUNT];
  double clos_f[ONTOLOGY_NODE_COUNT];
  for(size_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    double r = (double)bitset_popcount(&engine->by_direct_ontology[i]);
    double c = (double)bitset_popcount(&engine->by_closure_ontology[i]);
    raw_f[i] = r + 1.0 + 0.1 * (double)(i + 1u);
    clos_f[i] = c + 1.5 + 0.15 * (double)(i + 1u);
  }

  SheafCohomology cohom;
  ontology_sheaf_cohomology(raw_f, clos_f, &cohom);

  AdjunctionMonadState monad;
  ontology_adjunction_monad(raw_f, &monad);

  ToposState topos;
  engine_topos_state(engine, REGION_EAST_ASIA, REGION_EUROPE, ONTO_AI, ONTO_TECHNOLOGY, &topos);

  size_t severe_wx_cnt =
      bitset_popcount(&engine->by_weather_severity[WEATHER_WARNING]) +
      bitset_popcount(&engine->by_weather_severity[WEATHER_CATASTROPHIC]);
  double wx_forcing = (double)severe_wx_cnt / (double)(engine->article_count > 0 ? engine->article_count : 1u);
  ontology_atmospheric_dynamics(29.0, 74.0, 17.5, 45.0, wx_forcing, &out_app->weather_physics);

  double leinster_mag = ontology_leinster_magnitude();
  double weight_sum = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    weight_sum += ontology_node_magnitude_weight((OntologyNodeId)u);
  }
  uint32_t passed = 0;
  if(leinster_mag > 10.0 && fabs(leinster_mag - weight_sum) < 1e-11) passed++;              /* 1. Leinster Magnitude */
  if(cohom.euler_characteristic == 1 &&
     cohom.betti_numbers[0] == 1u && cohom.betti_numbers[1] == 0u) passed++;                /* 2. Nerve Euler-Poincare */
  if(cohom.cochain_exactness_norm < 1e-11) passed++;                                        /* 3. Hodge Cochain d^1 o d^0 = 0 */
  if(monad.zig_zag_triangle_defect < 1e-10 &&
     monad.beck_coequalizer_defect < 1e-10 &&
     monad.isbell_adjunction_defect < 1e-10) passed++;                                      /* 4. Kan/Beck/Isbell Monadicity */
  if(topos.spectral_seq.euler_e0 == 1 && topos.spectral_seq.euler_e_inf == 1 &&
     topos.spectral_seq.differential_nilpotency_defect < 1e-11) passed++;                   /* 5. Grothendieck Spectral Sequence */
  if(topos.day_adjunction_defect < 1e-10 &&
     topos.tensor_hom_adjunction_defect < 1e-10) passed++;                                  /* 6. Day & Weighted Tensor-Hom Adjunction */
  if(topos.sinkhorn_ot.cheeger_buser_holds == 1 &&
     topos.sinkhorn_ot.marginal_defect_l1 < 1e-6) passed++;                                 /* 7. Cheeger-Buser & Sinkhorn OT */
  if(topos.ricci_curv.gauss_bonnet_defect < 1e-10 &&
     topos.ricci_curv.ricci_flow_volume_defect < 1e-10) passed++;                           /* 8. Gauss-Bonnet & Ricci Flow */
  if(topos.zeta_tropical.ihara_det_at_zero_defect < 1e-10 &&
     topos.zeta_tropical.ihara_det_at_one_defect < 1e-10 &&
     topos.zeta_tropical.tropical_idempotence_defect < 1e-10) passed++;                     /* 9. Ihara-Bass & Tropical Hull */
  if(topos.rota_baxter.rota_baxter_identity_defect < 1e-10 &&
     topos.rota_baxter.spitzer_birkhoff_defect < 1e-10) passed++;                           /* 10. Rota-Baxter & Spitzer-Birkhoff */
  if(topos.quillen_weak_equivalence_holds == 1 &&
     topos.leibniz_rule_holds == 1) passed++;                                               /* 11. Quillen & Bi-Heyting Leibniz */
  if(out_app->weather_physics.lyapunov_max > 0.1 &&
     out_app->weather_physics.kaplan_yorke_dim > 2.0 &&
     out_app->weather_physics.kaplan_yorke_dim < 2.2) passed++;                             /* 12. Lorenz-63 Strange Attractor */

  double max_def = cohom.cochain_exactness_norm;
  if(monad.zig_zag_triangle_defect > max_def) max_def = monad.zig_zag_triangle_defect;
  if(topos.day_adjunction_defect > max_def) max_def = topos.day_adjunction_defect;
  if(topos.ricci_curv.gauss_bonnet_defect > max_def) max_def = topos.ricci_curv.gauss_bonnet_defect;
  if(topos.zeta_tropical.tropical_idempotence_defect > max_def) max_def = topos.zeta_tropical.tropical_idempotence_defect;
  if(topos.rota_baxter.rota_baxter_identity_defect > max_def) max_def = topos.rota_baxter.rota_baxter_identity_defect;

  out_app->theorems_verified_count = passed;
  out_app->theorems_total_count = 12u;
  out_app->all_theorems_verified = (passed == 12u) ? 1 : 0;
  out_app->leinster_magnitude = leinster_mag;
  out_app->euler_characteristic = cohom.euler_characteristic;
  out_app->fiedler_eigenvalue = cohom.fiedler_eigenvalue;
  out_app->cheeger_constant = topos.sinkhorn_ot.cheeger_constant;
  out_app->gauss_bonnet_total = topos.ricci_curv.gauss_bonnet_total;
  out_app->ihara_zeta_val = topos.zeta_tropical.ihara_zeta_val;
  out_app->tropical_eigenvalue = topos.zeta_tropical.tropical_eigenvalue;
  out_app->sinkhorn_divergence = topos.sinkhorn_ot.sinkhorn_divergence;
  out_app->fisher_rao_geodesic = topos.sinkhorn_ot.fisher_rao_geodesic;
  out_app->day_adjunction_defect = topos.day_adjunction_defect;
  out_app->rota_baxter_defect = topos.rota_baxter.rota_baxter_identity_defect;
  out_app->max_numerical_defect = max_def;

  /* 3. Cross-Domain Intelligence Situation Room */
  out_app->cyber_strategic_articles =
      bitset_popcount(&engine->by_clearance[CLEARANCE_ZERO_DAY]) +
      bitset_popcount(&engine->by_clearance[CLEARANCE_STRATEGIC]);
  out_app->frontier_ai_articles =
      bitset_popcount(&engine->by_ai_tier[AI_TIER_NEOCLOUD_CLUSTER]) +
      bitset_popcount(&engine->by_ai_tier[AI_TIER_FRONTIER_PRETRAINING]);
  out_app->severe_weather_articles = severe_wx_cnt;
  out_app->macro_active_articles =
      bitset_popcount(&engine->by_fiscal_stance[FISCAL_HAWKISH]) +
      bitset_popcount(&engine->by_fiscal_stance[FISCAL_DOVISH]) +
      bitset_popcount(&engine->by_fiscal_stance[FISCAL_BULLISH]) +
      bitset_popcount(&engine->by_fiscal_stance[FISCAL_BEARISH]);
  out_app->sports_articles = bitset_popcount(&engine->by_direct_ontology[ONTO_SPORTS]);

  TemporalSheafState temp;
  engine_temporal_sheaf(engine, &temp);
  const OntologyNode *em = ontology_node(temp.fastest_emerging_node);
  strncpy(out_app->emerging_category_slug, em ? em->slug : "ai", sizeof(out_app->emerging_category_slug) - 1u);
  out_app->emerging_acceleration_z = temp.acceleration[temp.fastest_emerging_node];

  /* 4. Autonomous Watchpoints & UCB1 Bandit Scheduler Summary */
  size_t active_rules = 0;
  for(size_t i = 0; i < engine->automation_rule_count; ++i) {
    if(engine->automation_rules[i].enabled) active_rules++;
  }
  out_app->active_watchpoint_rules = active_rules;
  out_app->total_watchpoint_alerts = engine->automation_alert_count;
  out_app->total_bandit_pulls = engine->total_bandit_pulls;
  out_app->auto_poll_interval_sec = engine->auto_poll_interval_sec;
  out_app->auto_cycles_completed = (uint32_t)engine->auto_cycles_completed;
  out_app->top_bandit_count = engine_bandit_select_feeds(engine, out_app->top_bandit_feeds, 4u);
}
