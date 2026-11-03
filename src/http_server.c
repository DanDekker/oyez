#include "http_server.h"

#include "markup.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int url_is_safe(const char *url) {
  if(!url) return 0;
  if(strncmp(url, "http://", 7u) != 0 && strncmp(url, "https://", 8u) != 0) return 0;
  for(const char *p = url; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if(c <= 0x20u || c >= 0x7Fu || c == '\'' || c == '"' || c == '`' || c == '$' || c == ';' ||
       c == '|' || c == '&' || c == '<' || c == '>' || c == '\\') {
      return 0;
    }
  }
  return 1;
}

int http_fetch_url(const char *url, Buffer *out) {
  if(!url_is_safe(url) || !out) return -1;
  buffer_init(out);
  char cmd[2048];
  int n = snprintf(cmd,
                   sizeof(cmd),
                   "curl -fsSL --max-time 12 -A 'Oyez/1.0 (+https://github.com/dandekker/oyez)' '%s' 2>/dev/null",
                   url);
  if(n < 0 || (size_t)n >= sizeof(cmd)) return -1;

  FILE *pipe = popen(cmd, "r");
  if(!pipe) return -1;

  char chunk[8192];
  size_t r = 0;
  while((r = fread(chunk, 1u, sizeof(chunk), pipe)) > 0) {
    buffer_append(out, chunk, r);
    if(out->len > 8u * 1024u * 1024u) break;
  }
  int status = pclose(pipe);
  if(status != 0 || out->len == 0) {
    buffer_free(out);
    return -1;
  }
  return 0;
}

int engine_refresh_feed(OyezEngine *engine, uint32_t feed_id, size_t *out_inserted) {
  if(out_inserted) *out_inserted = 0;
  FeedRecord *feed = NULL;
  for(size_t i = 0; i < engine->feed_count; ++i) {
    if(engine->feeds[i].id == feed_id) {
      feed = &engine->feeds[i];
      break;
    }
  }
  if(!feed) return -1;

  Buffer raw;
  if(http_fetch_url(feed->feed_url, &raw) != 0) {
    feed->error_count++;
    free(feed->last_error);
    feed->last_error = malloc(32u);
    if(feed->last_error) strcpy(feed->last_error, "fetch_failed");
    return -1;
  }

  ParsedFeed parsed;
  if(feed_parse(raw.data, raw.len, feed->site_url, &parsed) != 0) {
    buffer_free(&raw);
    feed->error_count++;
    free(feed->last_error);
    feed->last_error = malloc(32u);
    if(feed->last_error) strcpy(feed->last_error, "parse_failed");
    return -1;
  }
  buffer_free(&raw);

  size_t inserted = 0;
  for(size_t i = 0; i < parsed.item_count; ++i) {
    ParsedFeedItem *it = &parsed.items[i];
    int ins = 0;
    if(engine_ingest_article(engine,
                             feed->id,
                             it->guid,
                             it->title,
                             it->url,
                             it->author,
                             it->summary,
                             it->content_html,
                             it->published_at,
                             wall_time_ms(),
                             it->image_url,
                             (const char *const *)it->categories,
                             it->category_count,
                             NULL,
                             &ins) == 0 &&
       ins) {
      inserted++;
    }
  }

  feed->fetch_count++;
  feed->last_fetched_ms = wall_time_ms();
  free(feed->last_error);
  feed->last_error = malloc(1u);
  if(feed->last_error) feed->last_error[0] = '\0';
  feed_free(&parsed);
  if(out_inserted) *out_inserted = inserted;
  return 0;
}

size_t engine_poll_all(OyezEngine *engine, size_t max_feeds) {
  return engine_poll_concurrent(engine, max_feeds, 8u);
}

typedef struct {
  OyezEngine *engine;
  size_t limit;
  size_t next_idx;
  size_t total_inserted;
  pthread_mutex_t idx_lock;
  pthread_mutex_t eng_lock;
} PollPool;

static void *poll_worker_main(void *arg) {
  PollPool *pool = (PollPool *)arg;
  for(;;) {
    pthread_mutex_lock(&pool->idx_lock);
    size_t idx = pool->next_idx;
    if(idx >= pool->limit) {
      pthread_mutex_unlock(&pool->idx_lock);
      break;
    }
    pool->next_idx++;
    uint32_t feed_id = pool->engine->feeds[idx].id;
    const char *feed_url = pool->engine->feeds[idx].feed_url;
    const char *site_url = pool->engine->feeds[idx].site_url;
    pthread_mutex_unlock(&pool->idx_lock);

    Buffer raw;
    if(http_fetch_url(feed_url, &raw) != 0) {
      pthread_mutex_lock(&pool->eng_lock);
      pool->engine->feeds[idx].error_count++;
      pthread_mutex_unlock(&pool->eng_lock);
      continue;
    }

    ParsedFeed parsed;
    if(feed_parse(raw.data, raw.len, site_url, &parsed) != 0) {
      buffer_free(&raw);
      pthread_mutex_lock(&pool->eng_lock);
      pool->engine->feeds[idx].error_count++;
      pthread_mutex_unlock(&pool->eng_lock);
      continue;
    }
    buffer_free(&raw);

    pthread_mutex_lock(&pool->eng_lock);
    size_t ins_count = 0;
    for(size_t i = 0; i < parsed.item_count; ++i) {
      ParsedFeedItem *it = &parsed.items[i];
      int ins = 0;
      if(engine_ingest_article(pool->engine,
                               feed_id,
                               it->guid,
                               it->title,
                               it->url,
                               it->author,
                               it->summary,
                               it->content_html,
                               it->published_at,
                               wall_time_ms(),
                               it->image_url,
                               (const char *const *)it->categories,
                               it->category_count,
                               NULL,
                               &ins) == 0 &&
         ins) {
        ins_count++;
      }
    }
    pool->engine->feeds[idx].fetch_count++;
    pool->engine->feeds[idx].last_fetched_ms = wall_time_ms();
    pool->total_inserted += ins_count;
    pthread_mutex_unlock(&pool->eng_lock);

    feed_free(&parsed);
  }
  return NULL;
}

size_t engine_poll_concurrent(OyezEngine *engine, size_t max_feeds, size_t thread_count) {
  if(!engine || engine->feed_count == 0) return 0;
  size_t limit = (max_feeds > 0 && max_feeds < engine->feed_count) ? max_feeds : engine->feed_count;
  if(thread_count == 0) thread_count = 1u;
  if(thread_count > 16u) thread_count = 16u;
  if(thread_count > limit) thread_count = limit;

  PollPool pool;
  memset(&pool, 0, sizeof(pool));
  pool.engine = engine;
  pool.limit = limit;
  pthread_mutex_init(&pool.idx_lock, NULL);
  pthread_mutex_init(&pool.eng_lock, NULL);

  pthread_t threads[16];
  for(size_t t = 0; t < thread_count; ++t) {
    pthread_create(&threads[t], NULL, poll_worker_main, &pool);
  }
  for(size_t t = 0; t < thread_count; ++t) {
    pthread_join(threads[t], NULL);
  }

  pthread_mutex_destroy(&pool.idx_lock);
  pthread_mutex_destroy(&pool.eng_lock);
  return pool.total_inserted;
}

static void url_decode_Component(const char *src, size_t len, char *dst, size_t cap) {
  size_t out = 0;
  for(size_t i = 0; i < len && out + 1u < cap; ++i) {
    if(src[i] == '+') {
      dst[out++] = ' ';
    } else if(src[i] == '%' && i + 2u < len && isxdigit((unsigned char)src[i + 1u]) &&
              isxdigit((unsigned char)src[i + 2u])) {
      char hex[3] = {src[i + 1u], src[i + 2u], '\0'};
      dst[out++] = (char)strtol(hex, NULL, 16);
      i += 2u;
    } else {
      dst[out++] = src[i];
    }
  }
  dst[out] = '\0';
}

static int query_param(const char *query_str, const char *key, char *out_val, size_t cap) {
  out_val[0] = '\0';
  if(!query_str || !key) return 0;
  size_t klen = strlen(key);
  const char *p = query_str;
  while(*p) {
    const char *amp = strchr(p, '&');
    size_t slen = amp ? (size_t)(amp - p) : strlen(p);
    const char *eq = memchr(p, '=', slen);
    size_t namelen = eq ? (size_t)(eq - p) : slen;
    if(namelen == klen && strncmp(p, key, klen) == 0) {
      if(eq) {
        url_decode_Component(eq + 1u, slen - namelen - 1u, out_val, cap);
      }
      return 1;
    }
    if(!amp) break;
    p = amp + 1u;
  }
  return 0;
}

static void json_extract_field(const char *json, size_t len, const char *key, char *out, size_t cap) {
  out[0] = '\0';
  if(!json || len == 0) return;
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *hit = strstr(json, pat);
  if(!hit || (size_t)(hit - json) >= len) return;
  const char *p = hit + strlen(pat);
  const char *end = json + len;
  while(p < end && (isspace((unsigned char)*p) || *p == ':')) p++;
  if(p >= end || *p != '"') return;
  p++;
  size_t o = 0;
  while(p < end && *p != '"' && o + 1u < cap) {
    if(*p == '\\' && p + 1u < end) p++;
    out[o++] = *p++;
  }
  out[o] = '\0';
}

static void serialize_ontology_mask_array(Buffer *buf, uint32_t mask) {
  buffer_text(buf, "[");
  int first = 1;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    if(mask & (1u << i)) {
      if(!first) buffer_text(buf, ",");
      json_cstr(buf, ontology_node((OntologyNodeId)i)->slug);
      first = 0;
    }
  }
  buffer_text(buf, "]");
}

static void serialize_article(Buffer *buf, const ArticleRecord *a, double score, int include_content) {
  buffer_text(buf, "{");
  buffer_format(buf, "\"id\":%u,\"feed_id\":%u,", a->id, a->feed_id);
  buffer_text(buf, "\"guid\":"); json_cstr(buf, a->guid); buffer_text(buf, ",");
  buffer_text(buf, "\"title\":"); json_cstr(buf, a->title); buffer_text(buf, ",");
  buffer_text(buf, "\"url\":"); json_cstr(buf, a->url); buffer_text(buf, ",");
  buffer_text(buf, "\"author\":"); json_cstr(buf, a->author); buffer_text(buf, ",");
  buffer_text(buf, "\"summary\":"); json_cstr(buf, a->summary); buffer_text(buf, ",");
  if(include_content) {
    buffer_text(buf, "\"content_text\":"); json_cstr(buf, a->content_text); buffer_text(buf, ",");
    buffer_text(buf, "\"content_html\":"); json_cstr(buf, a->content_html); buffer_text(buf, ",");
  }
  buffer_text(buf, "\"published_at\":"); json_cstr(buf, a->published_at); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_title\":"); json_cstr(buf, a->feed_title); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_category\":"); json_cstr(buf, a->feed_category); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_region\":"); json_cstr(buf, world_region_slug(a->feed_region)); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_country\":"); json_cstr(buf, a->feed_country); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_language\":"); json_cstr(buf, a->feed_language); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_source_tier\":"); json_cstr(buf, a->feed_source_tier); buffer_text(buf, ",");
  buffer_text(buf, "\"news_type\":"); json_cstr(buf, news_type_slug(a->news_type)); buffer_text(buf, ",");
  buffer_text(buf, "\"clearance\":"); json_cstr(buf, clearance_level_slug(a->clearance)); buffer_text(buf, ",");
  buffer_format(buf, "\"opsec_score\":%.2f,", a->opsec_score);
  buffer_text(buf, "\"fiscal_stance\":"); json_cstr(buf, fiscal_stance_slug(a->fiscal_stance)); buffer_text(buf, ",");
  buffer_format(buf, "\"macro_score\":%.2f,", a->macro_score);
  buffer_text(buf, "\"mentioned_cves\":[");
  for(size_t i = 0; i < a->mentioned_cve_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_cves[i]);
  }
  buffer_text(buf, "],\"mentioned_assets\":[");
  for(size_t i = 0; i < a->mentioned_asset_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_assets[i]);
  }
  buffer_text(buf, "],\"mentioned_fiscal_events\":[");
  for(size_t i = 0; i < a->mentioned_fiscal_event_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_fiscal_events[i]);
  }
  buffer_text(buf, "],\"mentioned_topics\":");
  serialize_ontology_mask_array(buf, a->direct_ontology_mask);
  buffer_text(buf, ",\"category_closure\":");
  serialize_ontology_mask_array(buf, a->closure_ontology_mask);
  buffer_text(buf, ",\"mentioned_countries\":[");
  for(size_t i = 0; i < a->mentioned_country_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_countries[i]);
  }
  buffer_text(buf, "],\"mentioned_organizations\":[");
  for(size_t i = 0; i < a->mentioned_org_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_orgs[i]);
  }
  buffer_format(buf,
                "],\"word_count\":%u,\"reading_time_minutes\":%u,\"simhash64\":\"%016llx\",\"score\":%.4f}",
                a->word_count,
                a->reading_time_minutes,
                (unsigned long long)a->simhash64,
                score);
}

static void serialize_feed(Buffer *buf, const FeedRecord *f) {
  buffer_text(buf, "{");
  buffer_format(buf, "\"id\":%u,", f->id);
  buffer_text(buf, "\"title\":"); json_cstr(buf, f->title); buffer_text(buf, ",");
  buffer_text(buf, "\"feed_url\":"); json_cstr(buf, f->feed_url); buffer_text(buf, ",");
  buffer_text(buf, "\"site_url\":"); json_cstr(buf, f->site_url); buffer_text(buf, ",");
  buffer_text(buf, "\"category\":"); json_cstr(buf, f->category); buffer_text(buf, ",");
  buffer_text(buf, "\"region\":"); json_cstr(buf, world_region_slug(f->region)); buffer_text(buf, ",");
  buffer_text(buf, "\"country\":"); json_cstr(buf, f->country); buffer_text(buf, ",");
  buffer_text(buf, "\"subregion\":"); json_cstr(buf, f->subregion); buffer_text(buf, ",");
  buffer_text(buf, "\"language\":"); json_cstr(buf, f->language); buffer_text(buf, ",");
  buffer_text(buf, "\"source_tier\":"); json_cstr(buf, f->source_tier); buffer_text(buf, ",");
  buffer_text(buf, "\"editorial_focus\":"); json_cstr(buf, f->editorial_focus); buffer_text(buf, ",");
  buffer_format(buf, "\"article_count\":%u,\"fetch_count\":%u,\"error_count\":%u}",
                f->article_count, f->fetch_count, f->error_count);
}

int http_handle_request(ServerContext *ctx,
                        const char *method,
                        const char *target,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json) {
  if(!ctx || !method || !target || !out_status || !out_json) return -1;
  buffer_init(out_json);
  *out_status = 200;

  char path[512];
  const char *qmark = strchr(target, '?');
  size_t path_len = qmark ? (size_t)(qmark - target) : strlen(target);
  if(path_len >= sizeof(path)) path_len = sizeof(path) - 1u;
  memcpy(path, target, path_len);
  path[path_len] = '\0';
  while(path_len > 1u && path[path_len - 1u] == '/') {
    path[--path_len] = '\0';
  }
  const char *qs = qmark ? (qmark + 1u) : "";

  OyezEngine *eng = ctx->engine;

  if(strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/health") == 0)) {
    size_t cat_count = 0;
    catalog_entries(&cat_count);
    buffer_format(out_json,
                  "{\"name\":\"oyez\",\"version\":\"2.0.0-c23\",\"language\":\"C23\",\"status\":\"ok\","
                  "\"catalog_sources\":%zu,\"indexed_feeds\":%zu,\"indexed_articles\":%zu,\"inverted_terms\":%zu}",
                  cat_count,
                  eng->feed_count,
                  eng->article_count,
                  eng->inv_count);
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/stats") == 0) {
    size_t cat_count = 0;
    catalog_entries(&cat_count);
    buffer_format(out_json,
                  "{\"feeds\":%zu,\"articles\":%zu,\"inverted_terms\":%zu,\"catalog_size\":%zu,"
                  "\"tokenizer_vocab_size\":%u,\"regions\":{",
                  eng->feed_count,
                  eng->article_count,
                  eng->inv_count,
                  cat_count,
                  ctx->tokenizer ? m1_tokenizer_vocab_size(ctx->tokenizer) : 0u);
    for(uint32_t r = 0; r < WORLD_REGION_COUNT; ++r) {
      if(r > 0) buffer_text(out_json, ",");
      json_cstr(out_json, world_region_slug((WorldRegion)r));
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_region[r]));
    }
    buffer_text(out_json, "},\"news_types\":{");
    for(uint32_t nt = 0; nt < NEWS_TYPE_COUNT; ++nt) {
      if(nt > 0) buffer_text(out_json, ",");
      json_cstr(out_json, news_type_slug((NewsType)nt));
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_news_type[nt]));
    }
    buffer_text(out_json, "},\"clearance_levels\":{");
    for(uint32_t cl = 0; cl < CLEARANCE_LEVEL_COUNT; ++cl) {
      if(cl > 0) buffer_text(out_json, ",");
      json_cstr(out_json, clearance_level_slug((ClearanceLevel)cl));
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_clearance[cl]));
    }
    buffer_text(out_json, "},\"fiscal_stances\":{");
    for(uint32_t fs = 0; fs < FISCAL_STANCE_COUNT; ++fs) {
      if(fs > 0) buffer_text(out_json, ",");
      json_cstr(out_json, fiscal_stance_slug((FiscalStance)fs));
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_fiscal_stance[fs]));
    }
    buffer_text(out_json, "},\"categories_presheaf\":{");
    for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
      if(c > 0) buffer_text(out_json, ",");
      json_cstr(out_json, ontology_node((OntologyNodeId)c)->slug);
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_closure_ontology[c]));
    }
    buffer_text(out_json, "}}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/ontology") == 0) {
    char sub[64], sup[64], a_slug[64], b_slug[64];
    query_param(qs, "sub", sub, sizeof(sub));
    query_param(qs, "sup", sup, sizeof(sup));
    query_param(qs, "a", a_slug, sizeof(a_slug));
    query_param(qs, "b", b_slug, sizeof(b_slug));

    double raw_scores[ONTOLOGY_NODE_COUNT] = {0};
    double closure_scores[ONTOLOGY_NODE_COUNT] = {0};
    double kan_scores[ONTOLOGY_NODE_COUNT] = {0};
    double ran_scores[ONTOLOGY_NODE_COUNT] = {0};
    double mobius_recovered[ONTOLOGY_NODE_COUNT] = {0};
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      raw_scores[i] = (double)bitset_popcount(&eng->by_direct_ontology[i]);
      closure_scores[i] = (double)bitset_popcount(&eng->by_closure_ontology[i]);
    }
    ontology_left_kan_extend(raw_scores, kan_scores);
    ontology_right_kan_extend(closure_scores, ran_scores);
    ontology_mobius_invert(kan_scores, mobius_recovered);

    SheafCohomology sheaf;
    ontology_sheaf_cohomology(raw_scores, closure_scores, &sheaf);

    buffer_format(out_json,
                  "{\"terminal_object\":\"all-news\",\"node_count\":%d,\"sheaf_cohomology_l2_norm\":%.4f",
                  ONTOLOGY_NODE_COUNT,
                  sheaf.total_cohomology_norm);
    if(sub[0] && sup[0]) {
      OntologyNodeId u, v;
      int has = (ontology_from_slug(sub, &u) == 0 && ontology_from_slug(sup, &v) == 0 &&
                 ontology_has_morphism(u, v));
      buffer_format(out_json, ",\"morphism\":{\"sub\":\"%s\",\"sup\":\"%s\",\"exists\":%s}",
                    sub, sup, has ? "true" : "false");
    }
    if(a_slug[0] && b_slug[0]) {
      OntologyNodeId na, nb;
      if(ontology_from_slug(a_slug, &na) == 0 && ontology_from_slug(b_slug, &nb) == 0) {
        OntologyNodeId j = ontology_join(na, nb);
        uint32_t m = ontology_meet_mask(na, nb);
        uint32_t hey_imp = ontology_heyting_implication(1u << na, 1u << nb);
        uint32_t hey_neg = ontology_heyting_negation(1u << na);
        double w_dist = ontology_wasserstein_distance(1u << na, 1u << nb);
        buffer_format(out_json,
                      ",\"lattice\":{\"a\":\"%s\",\"b\":\"%s\",\"join\":\"%s\",\"wasserstein_distance\":%.4f,\"meet\":",
                      a_slug, b_slug, ontology_node(j)->slug, w_dist);
        serialize_ontology_mask_array(out_json, m);
        buffer_text(out_json, ",\"heyting_implication\":");
        serialize_ontology_mask_array(out_json, hey_imp);
        buffer_text(out_json, ",\"heyting_negation_a\":");
        serialize_ontology_mask_array(out_json, hey_neg);
        buffer_text(out_json, "}");
      }
    }
    buffer_text(out_json, ",\"nodes\":[");
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      const OntologyNode *n = ontology_node((OntologyNodeId)i);
      if(i > 0) buffer_text(out_json, ",");
      buffer_text(out_json, "{\"slug\":"); json_cstr(out_json, n->slug);
      buffer_text(out_json, ",\"label\":"); json_cstr(out_json, n->label);
      buffer_text(out_json, ",\"principal_filter\":");
      serialize_ontology_mask_array(out_json, n->upward_mask);
      buffer_text(out_json, ",\"principal_ideal\":");
      serialize_ontology_mask_array(out_json, n->downward_mask);
      buffer_format(out_json,
                    ",\"direct_fiber_size\":%u,\"presheaf_fiber_size\":%u,"
                    "\"left_kan_score\":%.2f,\"right_kan_score\":%.2f,"
                    "\"mobius_fundamental\":%.2f,\"cohomology_defect\":%.2f}",
                    bitset_popcount(&eng->by_direct_ontology[i]),
                    bitset_popcount(&eng->by_closure_ontology[i]),
                    kan_scores[i],
                    ran_scores[i],
                    mobius_recovered[i],
                    sheaf.cohomology_defect[i]);
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && (strcmp(path, "/api/v1/facets") == 0 || strcmp(path, "/api/v1/trending") == 0)) {
    StoryCluster clusters[24];
    size_t ccount = engine_story_clusters(eng, 18u, clusters, 24u);
    buffer_format(out_json, "{\"total_clusters\":%zu,\"clusters\":[", ccount);
    for(size_t i = 0; i < ccount; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_format(out_json,
                    "{\"cluster_id\":%u,\"member_count\":%zu,\"cluster_score\":%.2f,\"topics\":",
                    clusters[i].cluster_id,
                    clusters[i].member_count,
                    clusters[i].cluster_score);
      serialize_ontology_mask_array(out_json, clusters[i].ontology_mask);
      buffer_text(out_json, ",\"lead_article\":");
      serialize_article(out_json, clusters[i].lead_article, clusters[i].cluster_score, 0);
      buffer_text(out_json, "}");
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/concepts") == 0) {
    char min_s[32];
    query_param(qs, "min_support", min_s, sizeof(min_s));
    size_t min_supp = min_s[0] ? (size_t)strtoul(min_s, NULL, 10) : 1u;
    FormalConcept concepts[24];
    size_t count = engine_formal_concepts(eng, min_supp, concepts, 24u);
    buffer_format(out_json, "{\"total\":%zu,\"concepts\":[", count);
    for(size_t i = 0; i < count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_format(out_json, "{\"support\":%zu,\"stability_score\":%.2f,\"intent_categories\":",
                    concepts[i].extent_count, concepts[i].stability_score);
      serialize_ontology_mask_array(out_json, concepts[i].intent_ontology_mask);
      buffer_text(out_json, ",\"extent_article_ids\":[");
      for(size_t k = 0; k < concepts[i].extent_count; ++k) {
        if(k > 0) buffer_text(out_json, ",");
        buffer_format(out_json, "%u", concepts[i].extent_slots[k]);
      }
      buffer_text(out_json, "]}");
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/pullback") == 0) {
    char left[64], right[64], over[64];
    if(!query_param(qs, "left_region", left, sizeof(left)) || !left[0]) strcpy(left, "east-asia");
    if(!query_param(qs, "right_region", right, sizeof(right)) || !right[0]) strcpy(right, "europe");
    if(!query_param(qs, "over", over, sizeof(over)) || !over[0]) strcpy(over, "topic");

    PullbackPair pairs[32];
    size_t count = engine_categorical_pullback(eng, left, right, over, pairs, 32u);
    buffer_format(out_json,
                  "{\"left_region\":\"%s\",\"right_region\":\"%s\",\"over\":\"%s\",\"total\":%zu,\"pairs\":[",
                  left, right, over, count);
    for(size_t i = 0; i < count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_text(out_json, "{\"shared_witness\":");
      json_cstr(out_json, pairs[i].shared_witness);
      buffer_format(out_json, ",\"hamming_distance\":%u,\"coherence_score\":%.4f,\"left_article\":",
                    pairs[i].hamming_distance, pairs[i].coherence_score);
      serialize_article(out_json, pairs[i].left, 1.0, 0);
      buffer_text(out_json, ",\"right_article\":");
      serialize_article(out_json, pairs[i].right, 1.0, 0);
      buffer_text(out_json, "}");
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/centrality") == 0) {
    RankedEntity ents[24];
    ScoredArticle arts[16];
    size_t ecount = engine_bipartite_pagerank(eng, 15u, 0.85, ents, 24u, arts, 16u);
    size_t acount = eng->article_count < 16u ? eng->article_count : 16u;
    buffer_text(out_json, "{\"entities\":[");
    for(size_t i = 0; i < ecount; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_format(out_json, "{\"id\":\"%s\",\"label\":\"%s\",\"kind\":\"%s\",\"score\":%.6f,\"degree\":%u}",
                    ents[i].id, ents[i].label, ents[i].kind, ents[i].score, ents[i].degree);
    }
    buffer_text(out_json, "],\"top_articles\":[");
    for(size_t i = 0; i < acount; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      serialize_article(out_json, arts[i].article, arts[i].score, 0);
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/catalog") == 0) {
    char reg[64], ctry[16], cat[64];
    query_param(qs, "region", reg, sizeof(reg));
    query_param(qs, "country", ctry, sizeof(ctry));
    query_param(qs, "category", cat, sizeof(cat));

    size_t total = 0;
    const CatalogEntry *entries = catalog_entries(&total);
    buffer_text(out_json, "{\"sources\":[");
    size_t emitted = 0;
    for(size_t i = 0; i < total; ++i) {
      if(reg[0] && strcmp(world_region_slug(entries[i].region), reg) != 0) continue;
      if(ctry[0] && strcmp(entries[i].country, ctry) != 0) continue;
      if(cat[0] && strcmp(entries[i].category, cat) != 0) continue;
      if(emitted > 0) buffer_text(out_json, ",");
      buffer_text(out_json, "{\"title\":"); json_cstr(out_json, entries[i].title);
      buffer_text(out_json, ",\"feed_url\":"); json_cstr(out_json, entries[i].feed_url);
      buffer_text(out_json, ",\"site_url\":"); json_cstr(out_json, entries[i].site_url);
      buffer_text(out_json, ",\"category\":"); json_cstr(out_json, entries[i].category);
      buffer_text(out_json, ",\"region\":"); json_cstr(out_json, world_region_slug(entries[i].region));
      buffer_text(out_json, ",\"country\":"); json_cstr(out_json, entries[i].country);
      buffer_text(out_json, ",\"subregion\":"); json_cstr(out_json, entries[i].subregion);
      buffer_text(out_json, ",\"language\":"); json_cstr(out_json, entries[i].language);
      buffer_text(out_json, ",\"source_tier\":"); json_cstr(out_json, entries[i].source_tier);
      buffer_text(out_json, ",\"editorial_focus\":"); json_cstr(out_json, entries[i].editorial_focus);
      buffer_text(out_json, "}");
      emitted++;
    }
    buffer_format(out_json, "],\"total\":%zu}", emitted);
    return 0;
  }

  if(strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/catalog/seed") == 0) {
    char reg[64], ctry[16];
    json_extract_field(body, body_len, "region", reg, sizeof(reg));
    json_extract_field(body, body_len, "country", ctry, sizeof(ctry));
    size_t added = engine_seed_catalog(eng, reg[0] ? reg : NULL, ctry[0] ? ctry : NULL);
    if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
    buffer_format(out_json, "{\"seeded\":%zu,\"total_feeds\":%zu}", added, eng->feed_count);
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/feeds") == 0) {
    buffer_format(out_json, "{\"total\":%zu,\"feeds\":[", eng->feed_count);
    for(size_t i = 0; i < eng->feed_count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      serialize_feed(out_json, &eng->feeds[i]);
    }
    buffer_text(out_json, "]}");
    return 0;
  }

  if(strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/feeds/poll") == 0) {
    size_t ins = engine_poll_all(eng, 16u);
    if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
    buffer_format(out_json, "{\"articles_ingested\":%zu,\"total_articles\":%zu}", ins, eng->article_count);
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/markets") == 0) {
    buffer_text(out_json, "{\"fiscal_stances\":{");
    for(uint32_t fs = 0; fs < FISCAL_STANCE_COUNT; ++fs) {
      if(fs > 0) buffer_text(out_json, ",");
      json_cstr(out_json, fiscal_stance_slug((FiscalStance)fs));
      buffer_format(out_json, ":%u", bitset_popcount(&eng->by_fiscal_stance[fs]));
    }
    buffer_text(out_json, "},\"assets\":{");
    int first_a = 1;
    for(size_t i = 0; i < 128u; ++i) {
      if(eng->by_asset[i].occupied) {
        if(!first_a) buffer_text(out_json, ",");
        json_cstr(out_json, eng->by_asset[i].key);
        buffer_format(out_json, ":%u", bitset_popcount(&eng->by_asset[i].bits));
        first_a = 0;
      }
    }
    buffer_text(out_json, "},\"fiscal_events\":{");
    int first_e = 1;
    for(size_t i = 0; i < 64u; ++i) {
      if(eng->by_fiscal_event[i].occupied) {
        if(!first_e) buffer_text(out_json, ",");
        json_cstr(out_json, eng->by_fiscal_event[i].key);
        buffer_format(out_json, ":%u", bitset_popcount(&eng->by_fiscal_event[i].bits));
        first_e = 0;
      }
    }
    buffer_text(out_json, "}}");
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/articles") == 0) {
    char q_str[256], cat[64], morph[16], ntype[64], clr[64], fst[64], asset[32], fev[32];
    char reg[64], ctry[16], mctry[16], lang[16], tier[32];
    char div_str[16], lam_str[16], lim_str[16], off_str[16], fid_str[16];
    query_param(qs, "q", q_str, sizeof(q_str));
    query_param(qs, "category", cat, sizeof(cat));
    query_param(qs, "category_morphism", morph, sizeof(morph));
    query_param(qs, "news_type", ntype, sizeof(ntype));
    query_param(qs, "clearance", clr, sizeof(clr));
    query_param(qs, "fiscal_stance", fst, sizeof(fst));
    query_param(qs, "asset", asset, sizeof(asset));
    query_param(qs, "fiscal_event", fev, sizeof(fev));
    query_param(qs, "region", reg, sizeof(reg));
    query_param(qs, "country", ctry, sizeof(ctry));
    query_param(qs, "mentioned_country", mctry, sizeof(mctry));
    query_param(qs, "language", lang, sizeof(lang));
    query_param(qs, "source_tier", tier, sizeof(tier));
    query_param(qs, "diversify", div_str, sizeof(div_str));
    query_param(qs, "lambda", lam_str, sizeof(lam_str));
    query_param(qs, "limit", lim_str, sizeof(lim_str));
    query_param(qs, "offset", off_str, sizeof(off_str));
    query_param(qs, "feed_id", fid_str, sizeof(fid_str));

    ArticleQuery aq;
    memset(&aq, 0, sizeof(aq));
    aq.query = q_str[0] ? q_str : NULL;
    aq.category = cat[0] ? cat : NULL;
    aq.use_category_morphism = (strcmp(morph, "0") != 0 && strcmp(morph, "false") != 0);
    aq.news_type = ntype[0] ? ntype : NULL;
    aq.clearance = clr[0] ? clr : NULL;
    aq.fiscal_stance = fst[0] ? fst : NULL;
    aq.asset = asset[0] ? asset : NULL;
    aq.fiscal_event = fev[0] ? fev : NULL;
    aq.region = reg[0] ? reg : NULL;
    aq.country = ctry[0] ? ctry : NULL;
    aq.mentioned_country = mctry[0] ? mctry : NULL;
    aq.language = lang[0] ? lang : NULL;
    aq.source_tier = tier[0] ? tier : NULL;
    aq.feed_id = fid_str[0] ? (uint32_t)strtoul(fid_str, NULL, 10) : 0u;
    aq.diversify_mmr = (strcmp(div_str, "1") == 0 || strcmp(div_str, "true") == 0);
    aq.mmr_lambda = lam_str[0] ? strtod(lam_str, NULL) : 0.65;
    aq.limit = lim_str[0] ? (size_t)strtoul(lim_str, NULL, 10) : 50u;
    aq.offset = off_str[0] ? (size_t)strtoul(off_str, NULL, 10) : 0u;

    SearchResponse res;
    engine_search(eng, &aq, &res);
    buffer_format(out_json,
                  "{\"total\":%zu,\"count\":%zu,\"query_time_ms\":%.4f,\"articles\":[",
                  res.total, res.count, res.query_time_ms);
    for(size_t i = 0; i < res.count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      serialize_article(out_json, res.hits[i].article, res.hits[i].score, 0);
    }
    buffer_text(out_json, "]}");
    search_response_free(&res);
    return 0;
  }

  if(strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/articles/", 17u) == 0) {
    uint32_t id = (uint32_t)strtoul(path + 17u, NULL, 10);
    const ArticleRecord *a = engine_get_article_by_id(eng, id);
    if(!a) {
      *out_status = 404;
      buffer_text(out_json, "{\"error\":\"article_not_found\"}");
      return 0;
    }
    if(strstr(path + 17u, "/similar") != NULL) {
      const ArticleRecord *dups[16];
      uint32_t dists[16];
      size_t n = engine_find_near_duplicates(eng, id, 18u, dups, dists, 16u);
      buffer_format(out_json, "{\"article_id\":%u,\"similar\":[", id);
      for(size_t i = 0; i < n; ++i) {
        if(i > 0) buffer_text(out_json, ",");
        buffer_format(out_json, "{\"hamming_distance\":%u,\"article\":", dists[i]);
        serialize_article(out_json, dups[i], 1.0, 0);
        buffer_text(out_json, "}");
      }
      buffer_text(out_json, "]}");
      return 0;
    }
    buffer_text(out_json, "{\"article\":");
    serialize_article(out_json, a, 1.0, 1);
    buffer_text(out_json, "}");
    return 0;
  }

  if(strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/extract") == 0) {
    char html[32768], url[512];
    json_extract_field(body, body_len, "html", html, sizeof(html));
    json_extract_field(body, body_len, "url", url, sizeof(url));

    Buffer fetched;
    buffer_init(&fetched);
    const char *src_html = html;
    size_t src_len = strlen(html);
    if(src_len == 0 && url[0] != '\0') {
      if(http_fetch_url(url, &fetched) == 0) {
        src_html = fetched.data;
        src_len = fetched.len;
      }
    }
    if(src_len == 0) {
      buffer_free(&fetched);
      *out_status = 400;
      buffer_text(out_json, "{\"error\":\"missing_html_or_url\"}");
      return 0;
    }

    ExtractedContent ex;
    readability_extract(src_html, src_len, url, &ex);
    ExtractedEntities ent;
    entities_extract(ex.title, ex.excerpt, ex.text_content, NULL, NULL, 0, &ent);

    buffer_text(out_json, "{\"extracted\":{\"title\":"); json_cstr(out_json, ex.title);
    buffer_text(out_json, ",\"byline\":"); json_cstr(out_json, ex.byline);
    buffer_text(out_json, ",\"excerpt\":"); json_cstr(out_json, ex.excerpt);
    buffer_text(out_json, ",\"text_content\":"); json_cstr(out_json, ex.text_content);
    buffer_text(out_json, ",\"markdown\":"); json_cstr(out_json, ex.markdown);
    buffer_format(out_json, ",\"word_count\":%u,\"reading_time_minutes\":%u},\"entities\":{\"news_type\":",
                  ex.word_count, ex.reading_time_minutes);
    json_cstr(out_json, news_type_slug(ent.news_type));
    buffer_text(out_json, ",\"topics\":");
    serialize_ontology_mask_array(out_json, ent.direct_ontology_mask);
    buffer_text(out_json, ",\"category_closure\":");
    serialize_ontology_mask_array(out_json, ent.closure_ontology_mask);
    buffer_text(out_json, "}}");

    readability_free(&ex);
    buffer_free(&fetched);
    return 0;
  }

  if(strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/tokenize") == 0 && ctx->tokenizer) {
    char text[16384];
    json_extract_field(body, body_len, "text", text, sizeof(text));
    uint32_t *tokens = NULL;
    size_t count = 0;
    m1_tokenizer_encode(ctx->tokenizer, (const uint8_t *)text, strlen(text), &tokens, &count);
    buffer_format(out_json,
                  "{\"vocab_size\":%u,\"sha256\":\"%s\",\"token_count\":%zu,\"tokens\":[",
                  m1_tokenizer_vocab_size(ctx->tokenizer),
                  ctx->tokenizer->sha256,
                  count);
    for(size_t i = 0; i < count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_format(out_json, "%u", tokens[i]);
    }
    buffer_text(out_json, "]}");
    free(tokens);
    return 0;
  }

  *out_status = 404;
  buffer_text(out_json, "{\"error\":\"not_found\"}");
  return 0;
}

static const char *status_reason(int code) {
  switch(code) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    default: return "Internal Server Error";
  }
}

int http_server_listen(ServerContext *ctx) {
  if(!ctx) return -1;
  int srv = socket(AF_INET, SOCK_STREAM, 0);
  if(srv < 0) return -1;

  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ctx->port ? ctx->port : 8787u);

  if(bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(srv);
    return -1;
  }
  if(listen(srv, 64) < 0) {
    close(srv);
    return -1;
  }

  printf("oyez c23 server listening on http://127.0.0.1:%u (feeds=%zu, articles=%zu)\n",
         ctx->port ? ctx->port : 8787u,
         ctx->engine->feed_count,
         ctx->engine->article_count);
  fflush(stdout);

  for(;;) {
    int client = accept(srv, NULL, NULL);
    if(client < 0) continue;

    char req[65536];
    ssize_t n = recv(client, req, sizeof(req) - 1u, 0);
    if(n <= 0) {
      close(client);
      continue;
    }
    req[n] = '\0';

    char method[16] = {0};
    char target[1024] = {0};
    if(sscanf(req, "%15s %1023s", method, target) != 2) {
      close(client);
      continue;
    }

    if(strcmp(method, "OPTIONS") == 0) {
      const char *cors =
          "HTTP/1.1 204 No Content\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
          "Access-Control-Allow-Headers: Content-Type\r\n"
          "Connection: close\r\n\r\n";
      send(client, cors, strlen(cors), 0);
      close(client);
      continue;
    }

    const char *body = strstr(req, "\r\n\r\n");
    size_t body_len = 0;
    if(body) {
      body += 4u;
      body_len = (size_t)((req + n) - body);
    } else {
      body = "";
    }

    int status = 200;
    Buffer json;
    http_handle_request(ctx, method, target, body, body_len, &status, &json);

    Buffer resp;
    buffer_init(&resp);
    buffer_format(&resp,
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: application/json; charset=utf-8\r\n"
                  "Content-Length: %zu\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n",
                  status,
                  status_reason(status),
                  json.len);
    if(json.len > 0) buffer_append(&resp, json.data, json.len);

    if(resp.data && resp.len > 0) {
      send(client, resp.data, resp.len, 0);
    }
    buffer_free(&json);
    buffer_free(&resp);
    close(client);
  }
}
