#include "http_server.h"

#include "markup.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static void set_feed_error(FeedRecord *feed, const char *err) {
  if(!feed) return;
  size_t len = err ? strlen(err) : 0u;
  char *copy = malloc(len + 1u);
  if(copy) {
    if(len > 0u) memcpy(copy, err, len);
    copy[len] = '\0';
  }
  free(feed->last_error);
  feed->last_error = copy;
}

static size_t ingest_parsed_feed_items(OyezEngine *engine, FeedRecord *feed, const ParsedFeed *parsed) {
  size_t inserted = 0;
  for(size_t i = 0; i < parsed->item_count; ++i) {
    const ParsedFeedItem *it = &parsed->items[i];
    int ins = 0;
    uint64_t pub_ms = parse_timestamp_ms(it->published_at);
    if(pub_ms == 0u) pub_ms = (uint64_t)wall_time_ms();
    if(engine_ingest_article(engine,
                             feed->id,
                             it->guid,
                             it->title,
                             it->url,
                             it->author,
                             it->summary,
                             it->content_html,
                             it->published_at,
                             pub_ms,
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
  engine->total_bandit_pulls++;
  set_feed_error(feed, "");
  return inserted;
}

int engine_refresh_feed(OyezEngine *engine, uint32_t feed_id, size_t *out_inserted) {
  if(out_inserted) *out_inserted = 0;
  if(!engine) return -1;
  FeedRecord *feed = (FeedRecord *)engine_find_feed(engine, feed_id);
  if(!feed) return -1;

  Buffer raw;
  if(http_fetch_url(feed->feed_url, &raw) != 0) {
    feed->error_count++;
    set_feed_error(feed, "fetch_failed");
    return -1;
  }

  ParsedFeed parsed;
  if(feed_parse(raw.data, raw.len, feed->site_url, &parsed) != 0) {
    buffer_free(&raw);
    feed->error_count++;
    set_feed_error(feed, "parse_failed");
    return -1;
  }
  buffer_free(&raw);

  size_t inserted = ingest_parsed_feed_items(engine, feed, &parsed);
  feed_free(&parsed);
  if(out_inserted) *out_inserted = inserted;
  return 0;
}

size_t engine_poll_all(OyezEngine *engine, size_t max_feeds) {
  return engine_poll_concurrent(engine, max_feeds, 8u);
}

typedef struct {
  OyezEngine *engine;
  const uint32_t *feed_ids;
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
    size_t slot = pool->next_idx;
    if(slot >= pool->limit) {
      pthread_mutex_unlock(&pool->idx_lock);
      break;
    }
    pool->next_idx++;
    size_t idx = pool->feed_ids ? (size_t)(pool->feed_ids[slot] - 1u) : slot;
    if(idx >= pool->engine->feed_count) {
      pthread_mutex_unlock(&pool->idx_lock);
      continue;
    }
    const char *feed_url = pool->engine->feeds[idx].feed_url;
    const char *site_url = pool->engine->feeds[idx].site_url;
    pthread_mutex_unlock(&pool->idx_lock);

    Buffer raw;
    if(http_fetch_url(feed_url, &raw) != 0) {
      pthread_mutex_lock(&pool->eng_lock);
      pool->engine->feeds[idx].error_count++;
      set_feed_error(&pool->engine->feeds[idx], "fetch_failed");
      pool->engine->total_bandit_pulls++;
      pthread_mutex_unlock(&pool->eng_lock);
      continue;
    }

    ParsedFeed parsed;
    if(feed_parse(raw.data, raw.len, site_url, &parsed) != 0) {
      buffer_free(&raw);
      pthread_mutex_lock(&pool->eng_lock);
      pool->engine->feeds[idx].error_count++;
      set_feed_error(&pool->engine->feeds[idx], "parse_failed");
      pool->engine->total_bandit_pulls++;
      pthread_mutex_unlock(&pool->eng_lock);
      continue;
    }
    buffer_free(&raw);

    pthread_mutex_lock(&pool->eng_lock);
    pool->total_inserted += ingest_parsed_feed_items(pool->engine, &pool->engine->feeds[idx], &parsed);
    pthread_mutex_unlock(&pool->eng_lock);

    feed_free(&parsed);
  }
  return NULL;
}

static size_t run_poll_pool(OyezEngine *engine,
                            const uint32_t *feed_ids,
                            size_t limit,
                            size_t thread_count) {
  if(!engine || limit == 0) return 0;
  if(thread_count == 0) thread_count = 1u;
  if(thread_count > 16u) thread_count = 16u;
  if(thread_count > limit) thread_count = limit;

  PollPool pool;
  memset(&pool, 0, sizeof(pool));
  pool.engine = engine;
  pool.feed_ids = feed_ids;
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

size_t engine_poll_concurrent(OyezEngine *engine, size_t max_feeds, size_t thread_count) {
  if(!engine || engine->feed_count == 0) return 0;
  size_t limit = (max_feeds > 0 && max_feeds < engine->feed_count) ? max_feeds : engine->feed_count;
  return run_poll_pool(engine, NULL, limit, thread_count);
}

size_t engine_poll_bandit_auto(OyezEngine *engine, size_t batch_size, size_t thread_count) {
  if(!engine) return 0;
  size_t inserted = 0;
  if(engine->feed_count > 0 && batch_size > 0) {
    if(batch_size > 64u) batch_size = 64u;
    BanditFeedPriority prio[64];
    size_t selected = engine_bandit_select_feeds(engine, prio, batch_size);
    if(selected > 0) {
      uint32_t fids[64];
      for(size_t i = 0; i < selected; ++i) fids[i] = prio[i].feed_id;
      inserted = run_poll_pool(engine, fids, selected, thread_count);
    }
  }
  engine_evaluate_all_automation_rules(engine);
  engine->last_auto_cycle_ms = wall_time_ms();
  engine->auto_cycles_completed++;
  return inserted;
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
  if(!out || cap == 0) return;
  out[0] = '\0';
  if(!json || len == 0 || !key) return;
  char pat[64];
  int pn = snprintf(pat, sizeof(pat), "\"%s\"", key);
  if(pn <= 0 || (size_t)pn >= sizeof(pat)) return;
  size_t plen = (size_t)pn;
  if(len < plen) return;

  const char *hit = NULL;
  for(size_t i = 0; i + plen <= len; ++i) {
    if(memcmp(json + i, pat, plen) == 0) {
      hit = json + i;
      break;
    }
  }
  if(!hit) return;

  const char *p = hit + plen;
  const char *end = json + len;
  while(p < end && isspace((unsigned char)*p)) p++;
  if(p >= end || *p != ':') return;
  p++;
  while(p < end && isspace((unsigned char)*p)) p++;
  if(p >= end) return;
  if(*p == '"') {
    p++;
    size_t o = 0;
    while(p < end && *p != '"' && o + 1u < cap) {
      if(*p == '\\' && p + 1u < end) {
        p++;
        char c = *p++;
        if(c == 'n') c = '\n';
        else if(c == 'r') c = '\r';
        else if(c == 't') c = '\t';
        out[o++] = c;
      } else {
        out[o++] = *p++;
      }
    }
    out[o] = '\0';
  } else {
    size_t o = 0;
    while(p < end && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p) && o + 1u < cap) {
      out[o++] = *p++;
    }
    out[o] = '\0';
  }
}

static void query_or_json_param(const char *qs,
                                const char *body,
                                size_t body_len,
                                const char *key,
                                char *out,
                                size_t cap) {
  if(!query_param(qs, key, out, cap) || out[0] == '\0') {
    json_extract_field(body, body_len, key, out, cap);
  }
}

static void serialize_fiber_counts(Buffer *buf, const CountryFiber *table, size_t cap) {
  buffer_text(buf, "{");
  int first = 1;
  for(size_t i = 0; i < cap; ++i) {
    if(table[i].occupied) {
      if(!first) buffer_text(buf, ",");
      json_cstr(buf, table[i].key);
      buffer_format(buf, ":%u", bitset_popcount(&table[i].bits));
      first = 0;
    }
  }
  buffer_text(buf, "}");
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

static void serialize_ontology_score_map(Buffer *buf, const double *scores) {
  buffer_text(buf, "{");
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    if(c > 0u) buffer_text(buf, ",");
    json_cstr(buf, ontology_node((OntologyNodeId)c)->slug);
    buffer_format(buf, ":%.4f", scores[c]);
  }
  buffer_text(buf, "}");
}

static void serialize_rank_page_diagnostic(Buffer *buf, const SpectralSequenceState *rank) {
  buffer_format(buf, "{\"nerve_euler_characteristic\":%d,\"rank_strata\":[", rank->euler_e0);
  for(uint32_t p = 0; p < SPECTRAL_FILTRATION_RANKS; ++p) {
    if(p > 0u) buffer_text(buf, ",");
    serialize_ontology_mask_array(buf, rank->rank_strata_masks[p]);
  }
  buffer_text(buf, "],\"simplex_counts_by_rank\":[");
  for(uint32_t p = 0; p < SPECTRAL_FILTRATION_RANKS; ++p) {
    if(p > 0u) buffer_text(buf, ",");
    buffer_format(buf,
                  "[%u,%u,%u,%u]",
                  rank->e0_simplex_counts[p][0],
                  rank->e0_simplex_counts[p][1],
                  rank->e0_simplex_counts[p][2],
                  rank->e0_simplex_counts[p][3]);
  }
  buffer_format(buf,
                "],\"stratum_signal_norm\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                "\"neighbor_variation\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                "\"smoothed_rank_energy\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                "\"derived_pages_computed\":%s,\"diagnostic_only\":true}",
                rank->e1_stratum_energy[0], rank->e1_stratum_energy[1],
                rank->e1_stratum_energy[2], rank->e1_stratum_energy[3],
                rank->e1_stratum_energy[4], rank->e1_stratum_energy[5],
                rank->e2_derived_energy[0], rank->e2_derived_energy[1],
                rank->e2_derived_energy[2], rank->e2_derived_energy[3],
                rank->e2_derived_energy[4], rank->e2_derived_energy[5],
                rank->e_inf_abutment_energy[0], rank->e_inf_abutment_energy[1],
                rank->e_inf_abutment_energy[2], rank->e_inf_abutment_energy[3],
                rank->e_inf_abutment_energy[4], rank->e_inf_abutment_energy[5],
                rank->derived_pages_computed ? "true" : "false");
}

static void serialize_article_with_query(Buffer *buf,
                                         const ArticleRecord *a,
                                         double score,
                                         int include_content,
                                         const char *query) {
  buffer_text(buf, "{");
  buffer_format(buf, "\"id\":%u,\"feed_id\":%u,", a->id, a->feed_id);
  buffer_text(buf, "\"guid\":"); json_cstr(buf, a->guid); buffer_text(buf, ",");
  buffer_text(buf, "\"title\":"); json_cstr(buf, a->title); buffer_text(buf, ",");
  buffer_text(buf, "\"url\":"); json_cstr(buf, a->url); buffer_text(buf, ",");
  buffer_text(buf, "\"author\":"); json_cstr(buf, a->author); buffer_text(buf, ",");
  buffer_text(buf, "\"summary\":"); json_cstr(buf, a->summary); buffer_text(buf, ",");
  if(query && query[0] != '\0') {
    char snippet[280];
    engine_extract_query_snippet(a, query, snippet, sizeof(snippet));
    buffer_text(buf, "\"match_snippet\":");
    json_cstr(buf, snippet);
    buffer_text(buf, ",");
  }
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
  buffer_text(buf, "\"ai_tier\":"); json_cstr(buf, ai_compute_tier_slug(a->ai_tier)); buffer_text(buf, ",");
  buffer_format(buf, "\"ai_compute_score\":%.2f,", a->ai_compute_score);
  buffer_text(buf, "\"ai_license\":"); json_cstr(buf, ai_weight_license_slug(a->ai_license)); buffer_text(buf, ",");
  buffer_format(buf, "\"openness_score\":%.2f,\"sports_score\":%.2f,", a->openness_score, a->sports_score);
  buffer_text(buf, "\"weather_severity\":"); json_cstr(buf, weather_severity_slug(a->weather_severity)); buffer_text(buf, ",");
  buffer_format(buf, "\"weather_score\":%.2f,\"mentioned_weather\":[", a->weather_score);
  for(size_t i = 0; i < a->mentioned_weather_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_weather[i]);
  }
  buffer_text(buf, "],\"mentioned_sports\":[");
  for(size_t i = 0; i < a->mentioned_sport_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_sports[i]);
  }
  buffer_text(buf, "],\"mentioned_ai_models\":[");
  for(size_t i = 0; i < a->mentioned_ai_model_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_ai_models[i]);
  }
  buffer_text(buf, "],\"mentioned_ai_infra\":[");
  for(size_t i = 0; i < a->mentioned_ai_infra_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_ai_infra[i]);
  }
  buffer_text(buf, "],\"mentioned_hf_repos\":[");
  for(size_t i = 0; i < a->mentioned_hf_repo_count; ++i) {
    if(i > 0) buffer_text(buf, ",");
    json_cstr(buf, a->mentioned_hf_repos[i]);
  }
  buffer_text(buf, "],\"mentioned_cves\":[");
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

static void serialize_article(Buffer *buf, const ArticleRecord *a, double score, int include_content) {
  serialize_article_with_query(buf, a, score, include_content, NULL);
}

static double article_ai_score(const ArticleRecord *a) {
  return a ? a->ai_compute_score : 0.0;
}

static double article_sports_score(const ArticleRecord *a) {
  return a ? a->sports_score : 0.0;
}

static double article_weather_score(const ArticleRecord *a) {
  return a ? a->weather_score : 0.0;
}

static void serialize_top_articles_by_score(Buffer *buf,
                                            const OyezEngine *eng,
                                            double (*score_fn)(const ArticleRecord *),
                                            size_t max_k) {
  buffer_text(buf, "[");
  if(eng && score_fn && max_k > 0) {
    if(max_k > 16u) max_k = 16u;
    const ArticleRecord *top[16];
    double top_scores[16];
    size_t top_count = 0;
    for(size_t i = 0; i < eng->article_count; ++i) {
      const ArticleRecord *a = &eng->articles[i];
      double sc = score_fn(a);
      if(sc <= 0.0) continue;
      if(top_count < max_k) {
        top[top_count] = a;
        top_scores[top_count] = sc;
        top_count++;
      } else {
        size_t min_idx = 0;
        for(size_t k = 1u; k < max_k; ++k) {
          if(top_scores[k] < top_scores[min_idx]) min_idx = k;
        }
        if(sc > top_scores[min_idx]) {
          top[min_idx] = a;
          top_scores[min_idx] = sc;
        }
      }
    }
    for(size_t i = 0; i < top_count; ++i) {
      for(size_t j = i + 1u; j < top_count; ++j) {
        if(top_scores[j] > top_scores[i]) {
          const ArticleRecord *tmp_a = top[i];
          double tmp_s = top_scores[i];
          top[i] = top[j];
          top_scores[i] = top_scores[j];
          top[j] = tmp_a;
          top_scores[j] = tmp_s;
        }
      }
    }
    for(size_t i = 0; i < top_count; ++i) {
      if(i > 0) buffer_text(buf, ",");
      serialize_article(buf, top[i], top_scores[i], 0);
    }
  }
  buffer_text(buf, "]");
}

static int extract_html_from_request_or_url(const char *body,
                                            size_t body_len,
                                            const char *fallback_url,
                                            ExtractedContent *out_ex,
                                            int *out_status,
                                            Buffer *out_json,
                                            const char *missing_err_code) {
  char *html = malloc(65536u);
  if(!html) {
    *out_status = 500;
    buffer_text(out_json, "{\"error\":\"out_of_memory\"}");
    return -1;
  }
  char url[512];
  json_extract_field(body, body_len, "html", html, 65536u);
  json_extract_field(body, body_len, "url", url, sizeof(url));
  if(url[0] == '\0' && fallback_url) {
    snprintf(url, sizeof(url), "%s", fallback_url);
  }

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
    free(html);
    buffer_free(&fetched);
    *out_status = 400;
    buffer_format(out_json, "{\"error\":\"%s\"}", missing_err_code);
    return -1;
  }

  readability_extract(src_html, src_len, url, out_ex);
  free(html);
  buffer_free(&fetched);
  return 0;
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

static void serialize_biheyting_pair_masks(Buffer *buf,
                                           uint32_t hey_imp,
                                           uint32_t hey_neg,
                                           uint32_t lt_sheaf,
                                           uint32_t co_sub,
                                           uint32_t co_supp,
                                           uint32_t law_bd,
                                           uint32_t univ_q) {
  buffer_text(buf, ",\"heyting_implication\":");
  serialize_ontology_mask_array(buf, hey_imp);
  buffer_text(buf, ",\"heyting_negation_a\":");
  serialize_ontology_mask_array(buf, hey_neg);
  buffer_text(buf, ",\"lawvere_tierney_sheaf_a\":");
  serialize_ontology_mask_array(buf, lt_sheaf);
  buffer_text(buf, ",\"coheyting_subtraction\":");
  serialize_ontology_mask_array(buf, co_sub);
  buffer_text(buf, ",\"coheyting_supplement_a\":");
  serialize_ontology_mask_array(buf, co_supp);
  buffer_text(buf, ",\"lawvere_boundary_a\":");
  serialize_ontology_mask_array(buf, law_bd);
  buffer_text(buf, ",\"universal_quantifier_ab\":");
  serialize_ontology_mask_array(buf, univ_q);
}

static void serialize_u32_bit_indices(Buffer *buf, uint32_t mask) {
  buffer_text(buf, "[");
  int first = 1;
  for(uint32_t b = 0; b < 32u; ++b) {
    if(mask & (1u << b)) {
      if(!first) buffer_text(buf, ",");
      buffer_format(buf, "%u", b);
      first = 0;
    }
  }
  buffer_text(buf, "]");
}

static int handle_console(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)ctx; (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  static const char *const k_console_html_head =
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<title>Oyez // C99 Finite Poset &amp; Ideal Analytics</title>"
      "<style>"
      "body{margin:0;background:#080b11;color:#e2e8f0;font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace}"
      "header{padding:14px 20px;border-bottom:1px solid #1e293b;display:flex;justify-content:space-between;align-items:center;background:#0b111e}"
      "h1{margin:0;font-size:15px;color:#38bdf8;letter-spacing:.04em}"
      ".grid{display:grid;grid-template-columns:360px 1fr;gap:14px;padding:14px}"
      ".card{background:#0f172a;border:1px solid #1e293b;border-radius:6px;padding:12px;margin-bottom:12px}"
      ".card h2{margin:0 0 8px;font-size:12px;text-transform:uppercase;color:#94a3b8;letter-spacing:.06em}"
      ".stat-row{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #162032}"
      "input,select,button{background:#1e293b;color:#f8fafc;border:1px solid #334155;padding:6px 10px;border-radius:4px;font:inherit}"
      "button{background:#0284c7;border-color:#0369a1;cursor:pointer}"
      ".art{padding:10px;border-bottom:1px solid #1e293b}"
      ".art a{color:#38bdf8;text-decoration:none;font-weight:600}"
      ".badge{display:inline-block;padding:1px 6px;border-radius:3px;background:#1e293b;color:#7dd3fc;font-size:11px;margin-right:4px}"
      "</style></head><body>"
      "<header><h1>OYEZ // C99 FINITE POSET &amp; IDEAL ANALYTICS</h1>"
      "<div id=\"hdr-meta\">Loading C99 ontology engine...</div></header>"
      "<div class=\"grid\"><div>"
      "<div class=\"card\"><h2>One-Machine Appliance &amp; 12-Invariant Self-Check</h2><div id=\"app-stats\"></div>"
      "<div style=\"margin-top:8px\"><button onclick=\"runAutoSweep()\" style=\"width:100%\">Run Autonomous Sweep (UCB1 + Watchpoints)</button></div></div>"
      "<div class=\"card\"><h2>Category C &amp; Hodge Invariants</h2><div id=\"onto-stats\"></div></div>"
      "<div class=\"card\"><h2>Persistent Homology Barcode (beta_0, beta_1)</h2><div id=\"ph-stats\"></div></div>"
      "<div class=\"card\"><h2>Region&ndash;Topic Fiber Links</h2><div id=\"fib-list\"></div></div>"
      "</div><div>"
      "<div class=\"card\"><h2>BM25+ &amp; Poset Presheaf Fiber Search</h2>"
      "<div style=\"display:flex;gap:8px;margin-bottom:10px\">"
      "<input id=\"q\" placeholder=\"Search terms, CVEs, AI models, weather, tickers...\" style=\"flex:1\">"
      "<select id=\"cat\"><option value=\"all-news\">all-news (Terminal)</option>"
      "<option value=\"ai\">ai</option><option value=\"cyber\">cyber</option>"
      "<option value=\"cryptography\">cryptography</option><option value=\"finance\">finance</option>"
      "<option value=\"semiconductors\">semiconductors</option><option value=\"weather\">weather</option>"
      "<option value=\"climate\">climate</option><option value=\"sports\">sports</option></select>"
      "<button onclick=\"runSearch()\">Query Presheaf</button></div>"
      "<div id=\"results\"></div></div></div></div>";
    static const char *const k_console_html_script_1 =
      "<script>"
      "function esc(s){return String(s||'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}"
      "async function init(){"
      "const [st,ap,on,tm,fb]=await Promise.all([fetch('/api/v1/stats').then(r=>r.json()),"
      "fetch('/api/v1/appliance').then(r=>r.json()),fetch('/api/v1/ontology').then(r=>r.json()),"
      "fetch('/api/v1/temporal').then(r=>r.json()),fetch('/api/v1/fibration').then(r=>r.json())]);"
      "document.getElementById('hdr-meta').textContent="
      "`Feeds: ${st.feeds} | Articles: ${st.articles} | Terms: ${st.inverted_terms} | Checks: ${ap.checks_passed}/${ap.checks_total} | RAM: ${(ap.memory.total_resident_bytes/1048576).toFixed(2)} MB`;"
      "document.getElementById('app-stats').innerHTML="
      "`<div class='stat-row'><span>Appliance Consistency Audit</span><b>${ap.checks_passed}/${ap.checks_total} ${ap.all_checks_passed?'PASS':'DEGRADED'}</b></div>"
      "<div class='stat-row'><span>Resident Memory Footprint</span><b>${(ap.memory.total_resident_bytes/1048576).toFixed(2)} MB</b></div>"
      "<div class='stat-row'><span>String Arena (Used / Cap)</span><b>${(ap.memory.arena_bytes_used/1024).toFixed(1)} / ${(ap.memory.arena_bytes_capacity/1024).toFixed(1)} KB</b></div>"
      "<div class='stat-row'><span>Lorenz-63 Lyapunov / D_KY</span><b>${ap.atmospheric_physics.lyapunov_max.toFixed(4)} / ${ap.atmospheric_physics.kaplan_yorke_dim.toFixed(4)}</b></div>"
      "<div class='stat-row'><span>Stull Wet-Bulb / Rossby c_x</span><b>${ap.atmospheric_physics.wet_bulb_celsius.toFixed(2)} C / ${ap.atmospheric_physics.rossby_phase_speed.toFixed(2)} m/s</b></div>"
      "<div class='stat-row'><span>Watchpoint Rules / Alerts</span><b>${ap.automation.active_watchpoint_rules} / ${ap.automation.total_watchpoint_alerts}</b></div>`;";
    static const char *const k_console_html_script_2 =
      "document.getElementById('onto-stats').innerHTML="
      "`<div class='stat-row'><span>Leinster Magnitude |C|_L</span><b>${on.leinster_magnitude}</b></div>"
      "<div class='stat-row'><span>Euler Characteristic chi(N(C))</span><b>${on.euler_characteristic}</b></div>"
      "<div class='stat-row'><span>Fiedler Eigenvalue l_2(L_0)</span><b>${on.fiedler_eigenvalue}</b></div>"
      "<div class='stat-row'><span>Hodge Gradient Norm ||d_0 phi||</span><b>${on.hodge_gradient_norm}</b></div>"
      "<div class='stat-row'><span>Hodge Coexact Norm ||d_1^T psi||</span><b>${on.hodge_coexact_norm}</b></div>"
      "<div class='stat-row'><span>Hodge Harmonic Residual</span><b>${on.hodge_harmonic_norm}</b></div>"
      "<div class='stat-row'><span>1-Laplacian Trace Tr(L_1)</span><b>${on.laplacian_1_trace}</b></div>"
      "<div class='stat-row'><span>Isbell Envelope Defect</span><b>${on.isbell_codensity_defect}</b></div>"
      "<div class='stat-row'><span>Cochain Exactness ||d_1 d_0||</span><b>${on.cochain_exactness_norm}</b></div>"
      "<div class='stat-row'><span>Kleisli Monad Entropy</span><b>${tm.adjunction_monad.kleisli_entropy}</b></div>"
      "<div class='stat-row'><span>Fastest Emerging Stalk</span><b>${tm.temporal_sheaf.fastest_emerging_category}</b></div>`;"
      "document.getElementById('ph-stats').innerHTML=tm.persistent_homology.filtration_steps.map(s=>"
      "`<div class='stat-row'><span>eps=${s.threshold_eps.toFixed(2)} (V=${s.f_vector[0]},E=${s.f_vector[1]})</span>"
      "<b>b0=${s.betti_0}, b1=${s.betti_1}</b></div>`).join('');"
      "document.getElementById('fib-list').innerHTML=(fb.cartesian_lifts||[]).slice(0,5).map(l=>"
      "`<div class='stat-row'><span>${l.sub_category} -> ${l.sup_category}</span>"
      "<span class='badge'>${l.transport_witness}</span></div>`).join('');"
      "runSearch();}"
      "async function runAutoSweep(){"
      "await fetch('/api/v1/automation/run',{method:'POST',body:'{}'});"
      "await init();}"
      "async function runSearch(){"
      "const q=encodeURIComponent(document.getElementById('q').value);"
      "const c=encodeURIComponent(document.getElementById('cat').value);"
      "const r=await fetch(`/api/v1/articles?q=${q}&category=${c}&limit=20`).then(x=>x.json());"
      "document.getElementById('results').innerHTML=(r.articles||[]).map(a=>"
      "`<div class='art'><a href='${esc(a.url)}' target='_blank' rel='noopener'>${esc(a.title)}</a>"
      "<div><span class='badge'>${esc(a.feed_country)}</span><span class='badge'>${esc(a.news_type)}</span>"
      "<span class='badge'>${esc(a.ai_tier)}</span><span class='badge'>${esc(a.clearance)}</span>"
      "<span class='badge'>${esc(a.weather_severity)}</span></div></div>`).join('');}"
      "init();</script></body></html>";
  buffer_text(out_json, k_console_html_head);
  buffer_text(out_json, k_console_html_script_1);
  buffer_text(out_json, k_console_html_script_2);
  return 0;
}

static int handle_health(ServerContext *ctx,
                         const char *path,
                         const char *qs,
                         const char *body,
                         size_t body_len,
                         int *out_status,
                         Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  size_t cat_count = 0;
  catalog_entries(&cat_count);
  buffer_format(out_json,
                "{\"name\":\"oyez\",\"version\":\"2.0.0-c99\",\"language\":\"C99\",\"status\":\"ok\","
                "\"catalog_sources\":%zu,\"indexed_feeds\":%zu,\"indexed_articles\":%zu,\"inverted_terms\":%zu}",
                cat_count,
                eng->feed_count,
                eng->article_count,
                eng->inv_count);
  return 0;
}

static int handle_stats(ServerContext *ctx,
                        const char *path,
                        const char *qs,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  size_t cat_count = 0;
  catalog_entries(&cat_count);
  buffer_format(out_json,
                "{\"feeds\":%zu,\"articles\":%zu,\"inverted_terms\":%zu,\"catalog_size\":%zu,"
                "\"tokenizer_vocab_size\":%u,\"automation_rules\":%zu,\"automation_alerts\":%zu,\"regions\":{",
                eng->feed_count,
                eng->article_count,
                eng->inv_count,
                cat_count,
                ctx->tokenizer ? m1_tokenizer_vocab_size(ctx->tokenizer) : 0u,
                eng->automation_rule_count,
                eng->automation_alert_count);
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
  buffer_text(out_json, "},\"weather_severities\":{");
  for(uint32_t ws = 0; ws < WEATHER_SEVERITY_COUNT; ++ws) {
    if(ws > 0) buffer_text(out_json, ",");
    json_cstr(out_json, weather_severity_slug((WeatherSeverity)ws));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_weather_severity[ws]));
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

static int handle_ontology(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
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
    double yoneda_kan[ONTOLOGY_NODE_COUNT];
    ontology_yoneda_kan_transform(raw_scores, yoneda_kan);

    SheafCohomology sheaf;
    ontology_sheaf_cohomology(raw_scores, closure_scores, &sheaf);
    double coend_self = ontology_profunctor_coend(closure_scores, closure_scores);
    double end_self = ontology_profunctor_end(closure_scores, closure_scores);
    double w_colim_self = ontology_weighted_colimit(raw_scores, closure_scores);
    double w_lim_self = ontology_weighted_limit(raw_scores, closure_scores);
    int buser_holds = 0;
    double cheeger_h = ontology_cheeger_constant(&buser_holds);
    double cocone_defect_self = ontology_cocone_adjunction_defect(raw_scores, closure_scores, kan_scores);
    OntologyOrderAudit order_audit;
    ontology_order_audit(&order_audit);

    buffer_format(out_json,
                  "{\"terminal_object\":\"all-news\",\"node_count\":%d,\"sheaf_cohomology_l2_norm\":%.4f,"
                  "\"nerve_f_vector\":[%u,%u,%u,%u,%u,%u],\"euler_characteristic\":%d,"
                  "\"betti_numbers\":[%u,%u],\"mobius_total_variation\":%u,"
                  "\"laplacian_trace\":%.2f,\"laplacian_1_trace\":%.2f,\"spectral_radius\":%.4f,\"fiedler_eigenvalue\":%.4f,"
                  "\"cheeger_constant\":%.4f,\"cheeger_buser_holds\":%s,"
                  "\"dirichlet_energy\":%.2f,\"cochain_exactness_norm\":%.6e,"
                  "\"hodge_1_curl_norm\":%.4f,\"hodge_gradient_norm\":%.4f,\"hodge_solenoidal_norm\":%.4f,"
                  "\"hodge_coexact_norm\":%.4f,\"hodge_harmonic_norm\":%.4f,"
                  "\"leinster_magnitude\":%.4f,\"isbell_codensity_defect\":%.6e,\"monad_closure_defect\":%.4f,"
                  "\"profunctor_coend_self\":%.2f,\"profunctor_end_self\":%.4f,"
                  "\"weighted_colimit_self\":%.4f,\"weighted_limit_self\":%.4f,"
                  "\"cocone_adjunction_defect_self\":%.6e,"
                  "\"day_adjunction_defect_self\":%.6e",
                  ONTOLOGY_NODE_COUNT,
                  sheaf.total_cohomology_norm,
                  sheaf.nerve_f_vector[0],
                  sheaf.nerve_f_vector[1],
                  sheaf.nerve_f_vector[2],
                  sheaf.nerve_f_vector[3],
                  sheaf.nerve_f_vector[4],
                  sheaf.nerve_f_vector[5],
                  sheaf.euler_characteristic,
                  sheaf.betti_numbers[0],
                  sheaf.betti_numbers[1],
                  sheaf.mobius_total_variation,
                  sheaf.laplacian_trace,
                  sheaf.laplacian_1_trace,
                  sheaf.spectral_radius,
                  sheaf.fiedler_eigenvalue,
                  cheeger_h,
                  buser_holds ? "true" : "false",
                  sheaf.dirichlet_energy,
                  sheaf.cochain_exactness_norm,
                  sheaf.hodge_1_curl_norm,
                  sheaf.hodge_gradient_norm,
                  sheaf.hodge_solenoidal_norm,
                  sheaf.hodge_coexact_norm,
                  sheaf.hodge_harmonic_norm,
                  sheaf.leinster_magnitude,
                  sheaf.isbell_codensity_defect,
                  sheaf.monad_closure_defect,
                  coend_self,
                  end_self,
                  w_colim_self,
                  w_lim_self,
                  cocone_defect_self,
                  cocone_defect_self);
    buffer_format(out_json,
                  ",\"order_audit\":{\"object_count\":%u,\"morphism_count\":%u,\"pair_count\":%u,"
                  "\"unique_join_pairs\":%u,\"ambiguous_join_pairs\":%u,\"missing_join_pairs\":%u,"
                  "\"unique_meet_pairs\":%u,\"ambiguous_meet_pairs\":%u,\"missing_meet_pairs\":%u,"
                  "\"is_reflexive\":%s,\"is_antisymmetric\":%s,\"is_transitive\":%s,\"is_lattice\":%s}",
                  order_audit.object_count,
                  order_audit.morphism_count,
                  order_audit.pair_count,
                  order_audit.unique_join_pairs,
                  order_audit.ambiguous_join_pairs,
                  order_audit.missing_join_pairs,
                  order_audit.unique_meet_pairs,
                  order_audit.ambiguous_meet_pairs,
                  order_audit.missing_meet_pairs,
                  order_audit.is_reflexive ? "true" : "false",
                  order_audit.is_antisymmetric ? "true" : "false",
                  order_audit.is_transitive ? "true" : "false",
                  order_audit.is_lattice ? "true" : "false");
    if(sub[0] && sup[0]) {
      OntologyNodeId u, v;
      int has = (ontology_from_slug(sub, &u) == 0 && ontology_from_slug(sup, &v) == 0 &&
                 ontology_has_morphism(u, v));
      int32_t mu = (ontology_from_slug(sub, &u) == 0 && ontology_from_slug(sup, &v) == 0)
                   ? ontology_mobius_value(u, v) : 0;
      buffer_text(out_json, ",\"morphism\":{\"sub\":");
      json_cstr(out_json, sub);
      buffer_text(out_json, ",\"sup\":");
      json_cstr(out_json, sup);
      buffer_format(out_json, ",\"exists\":%s,\"mobius_mu\":%d}", has ? "true" : "false", mu);
    }
    if(a_slug[0] && b_slug[0]) {
      OntologyNodeId na, nb;
      if(ontology_from_slug(a_slug, &na) == 0 && ontology_from_slug(b_slug, &nb) == 0) {
        OntologyNodeId unique_join, unique_meet;
        uint32_t minimal_upper_bounds = ontology_minimal_upper_bounds(na, nb);
        uint32_t maximal_lower_bounds = ontology_maximal_lower_bounds(na, nb);
        int has_unique_join = ontology_unique_join(na, nb, &unique_join) == 0;
        int has_unique_meet = ontology_unique_meet(na, nb, &unique_meet) == 0;
        uint32_t ideal_a = ontology_node(na)->downward_mask;
        uint32_t ideal_b = ontology_node(nb)->downward_mask;
        uint32_t ideal_join = ontology_ideal_join(ideal_a, ideal_b);
        uint32_t ideal_meet = ontology_ideal_meet(ideal_a, ideal_b);
        uint32_t characteristic_sieve_a_at_b = ontology_subobject_classifier_sieve(ideal_a, nb);
        uint32_t hey_imp = ontology_heyting_implication(1u << na, 1u << nb);
        uint32_t hey_neg = ontology_heyting_negation(1u << na);
        uint32_t lt_sheaf = ontology_lawvere_tierney_sheafify(1u << na);
        uint32_t co_sub = ontology_coheyting_subtraction(1u << na, 1u << nb);
        uint32_t co_supp = ontology_coheyting_negation(1u << na);
        uint32_t law_bd = ontology_coheyting_boundary(1u << na);
        uint32_t univ_q = ontology_universal_quantifier(ontology_downward_closure((1u << na) | (1u << nb)));
        int dense_a = ontology_is_grothendieck_dense(1u << na);
        double w_dist = ontology_wasserstein_distance(1u << na, 1u << nb);
        double l_ab = ontology_lawvere_distance(1u << na, 1u << nb);
        double l_ba = ontology_lawvere_distance(1u << nb, 1u << na);
        buffer_text(out_json, ",\"order\":{\"a\":");
        json_cstr(out_json, a_slug);
        buffer_text(out_json, ",\"b\":");
        json_cstr(out_json, b_slug);
        buffer_text(out_json, ",\"minimal_upper_bounds\":");
        serialize_ontology_mask_array(out_json, minimal_upper_bounds);
        buffer_text(out_json, ",\"maximal_lower_bounds\":");
        serialize_ontology_mask_array(out_json, maximal_lower_bounds);
        buffer_text(out_json, ",\"unique_join\":");
        if(has_unique_join) json_cstr(out_json, ontology_node(unique_join)->slug);
        else buffer_text(out_json, "null");
        buffer_text(out_json, ",\"unique_meet\":");
        if(has_unique_meet) json_cstr(out_json, ontology_node(unique_meet)->slug);
        else buffer_text(out_json, "null");
        buffer_text(out_json, ",\"ideal_join\":");
        serialize_ontology_mask_array(out_json, ideal_join);
        buffer_text(out_json, ",\"ideal_meet\":");
        serialize_ontology_mask_array(out_json, ideal_meet);
        buffer_text(out_json, ",\"characteristic_sieve_a_at_b\":");
        serialize_ontology_mask_array(out_json, characteristic_sieve_a_at_b);
        buffer_format(out_json,
                      ",\"wasserstein_distance\":%.4f,\"lawvere_distance_ab\":%.4f,"
                      "\"lawvere_distance_ba\":%.4f,\"grothendieck_dense_a\":%s",
                      w_dist, l_ab, l_ba,
                      dense_a ? "true" : "false");
        serialize_biheyting_pair_masks(out_json, hey_imp, hey_neg, lt_sheaf,
                                       co_sub, co_supp, law_bd, univ_q);
        buffer_text(out_json, "}");

        /* Deprecated v1 alias.  Ambiguous joins are null rather than an
         * arbitrary topic, which is the intentional correctness break. */
        buffer_text(out_json, ",\"lattice\":{\"deprecated\":true,\"a\":");
        json_cstr(out_json, a_slug);
        buffer_text(out_json, ",\"b\":");
        json_cstr(out_json, b_slug);
        buffer_text(out_json, ",\"join\":");
        if(has_unique_join) json_cstr(out_json, ontology_node(unique_join)->slug);
        else buffer_text(out_json, "null");
        buffer_format(out_json,
                      ",\"wasserstein_distance\":%.4f,\"lawvere_distance_ab\":%.4f,"
                      "\"lawvere_distance_ba\":%.4f,\"grothendieck_dense_a\":%s,\"meet\":",
                      w_dist, l_ab, l_ba,
                      dense_a ? "true" : "false");
        serialize_ontology_mask_array(out_json, maximal_lower_bounds);
        serialize_biheyting_pair_masks(out_json, hey_imp, hey_neg, lt_sheaf,
                                       co_sub, co_supp, law_bd, univ_q);
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
                    "\"yoneda_kan_score\":%.2f,\"leinster_weight\":%.4f,"
                    "\"mobius_fundamental\":%.2f,\"cohomology_defect\":%.2f,"
                    "\"information_specificity\":%.4f}",
                    bitset_popcount(&eng->by_direct_ontology[i]),
                    bitset_popcount(&eng->by_closure_ontology[i]),
                    kan_scores[i],
                    ran_scores[i],
                    yoneda_kan[i],
                    ontology_node_magnitude_weight((OntologyNodeId)i),
                    mobius_recovered[i],
                    sheaf.cohomology_defect[i],
                    ontology_information_specificity(1u << i));
    }
    buffer_text(out_json, "]}");
    return 0;
}

static int handle_facets(ServerContext *ctx,
                         const char *path,
                         const char *qs,
                         const char *body,
                         size_t body_len,
                         int *out_status,
                         Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
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

static int handle_concepts(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char min_s[32];
  query_param(qs, "min_support", min_s, sizeof(min_s));
  size_t min_supp = min_s[0] ? (size_t)strtoul(min_s, NULL, 10) : 1u;
  FormalConcept concepts[24];
  size_t count = engine_formal_concepts(eng, min_supp, concepts, 24u);
  buffer_format(out_json, "{\"total\":%zu,\"concepts\":[", count);
  for(size_t i = 0; i < count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_format(out_json,
                  "{\"concept_id\":%u,\"support\":%zu,\"stability_score\":%.2f,"
                  "\"intensional_entropy\":%.4f,\"intent_categories\":",
                  concepts[i].concept_id,
                  concepts[i].extent_count,
                  concepts[i].stability_score,
                  concepts[i].intensional_entropy);
    serialize_ontology_mask_array(out_json, concepts[i].intent_ontology_mask);
    buffer_text(out_json, ",\"lawvere_tierney_closure\":");
    serialize_ontology_mask_array(out_json, concepts[i].lawvere_tierney_mask);
    buffer_text(out_json, ",\"subconcept_ids\":");
    serialize_u32_bit_indices(out_json, concepts[i].subconcept_mask);
    buffer_text(out_json, ",\"superconcept_ids\":");
    serialize_u32_bit_indices(out_json, concepts[i].superconcept_mask);
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

static int handle_pullback(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char left[64], right[64], over[64];
  if(!query_param(qs, "left_region", left, sizeof(left)) || !left[0]) strcpy(left, "east-asia");
  if(!query_param(qs, "right_region", right, sizeof(right)) || !right[0]) strcpy(right, "europe");
  if(!query_param(qs, "over", over, sizeof(over)) || !over[0]) strcpy(over, "topic");

  PullbackPair pairs[32];
  size_t count = engine_categorical_pullback(eng, left, right, over, pairs, 32u);
  buffer_text(out_json, "{\"left_region\":");
  json_cstr(out_json, left);
  buffer_text(out_json, ",\"right_region\":");
  json_cstr(out_json, right);
  buffer_text(out_json, ",\"over\":");
  json_cstr(out_json, over);
  buffer_format(out_json, ",\"total\":%zu,\"pairs\":[", count);
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

static int handle_pushout(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char left[64], right[64], over[64];
  if(!query_param(qs, "left_region", left, sizeof(left)) || !left[0]) strcpy(left, "east-asia");
  if(!query_param(qs, "right_region", right, sizeof(right)) || !right[0]) strcpy(right, "europe");
  if(!query_param(qs, "over", over, sizeof(over)) || !over[0]) strcpy(over, "topic");

  PushoutAmalgam amalgams[32];
  size_t count = engine_categorical_pushout(eng, left, right, over, amalgams, 32u);

  WorldRegion lr = REGION_GLOBAL, rr = REGION_GLOBAL;
  world_region_from_slug(left, &lr);
  world_region_from_slug(right, &rr);
  PresheafMorphism morph;
  engine_natural_transformation(eng, lr, rr, &morph);

  buffer_text(out_json, "{\"left_region\":");
  json_cstr(out_json, left);
  buffer_text(out_json, ",\"right_region\":");
  json_cstr(out_json, right);
  buffer_text(out_json, ",\"over\":");
  json_cstr(out_json, over);
  buffer_format(out_json,
                ",\"total\":%zu,"
                "\"natural_transformation\":{\"naturality_defect\":%.4f,\"coend_pairing\":%.2f,"
                "\"lawvere_transport_cost\":%.4f,\"reverse_transport_cost\":%.4f,\"heyting_truth_sieve\":",
                count,
                morph.naturality_defect,
                morph.coend_pairing,
                morph.lawvere_transport_cost,
                morph.reverse_transport_cost);
  serialize_ontology_mask_array(out_json, morph.heyting_truth_mask);
  buffer_text(out_json, ",\"dense_sieve_closure\":");
  serialize_ontology_mask_array(out_json, morph.dense_sieve_mask);
  buffer_text(out_json, "},\"amalgams\":[");
  for(size_t i = 0; i < count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_text(out_json, "{\"shared_witness\":");
    json_cstr(out_json, amalgams[i].shared_witness);
    buffer_format(out_json,
                  ",\"member_count\":%zu,\"left_count\":%u,\"right_count\":%u,"
                  "\"colimit_cocone_score\":%.4f,\"coproduct_topics\":",
                  amalgams[i].member_count,
                  amalgams[i].left_count,
                  amalgams[i].right_count,
                  amalgams[i].colimit_cocone_score);
    serialize_ontology_mask_array(out_json, amalgams[i].coproduct_ontology_mask);
    buffer_text(out_json, ",\"sheafified_topics\":");
    serialize_ontology_mask_array(out_json, amalgams[i].sheafified_ontology_mask);
    buffer_text(out_json, ",\"shared_closure\":");
    serialize_ontology_mask_array(out_json, amalgams[i].shared_closure_mask);
    buffer_text(out_json, ",\"member_article_ids\":[");
    for(size_t m = 0; m < amalgams[i].member_count; ++m) {
      if(m > 0) buffer_text(out_json, ",");
      buffer_format(out_json, "%u", amalgams[i].member_ids[m]);
    }
    buffer_text(out_json, "]}");
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_fibration(ServerContext *ctx,
                            const char *path,
                            const char *qs,
                            const char *body,
                            size_t body_len,
                            int *out_status,
                            Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char cat[64];
  if(!query_param(qs, "category", cat, sizeof(cat)) || !cat[0]) strcpy(cat, "all-news");
  CartesianLift lifts[32];
  size_t count = engine_grothendieck_fibration(eng, cat, lifts, 32u);
  buffer_text(out_json, "{\"base_category\":");
  json_cstr(out_json, cat);
  buffer_format(out_json,
                ",\"leinster_magnitude\":%.4f,\"total\":%zu,\"cartesian_lifts\":[",
                ontology_leinster_magnitude(),
                count);
  for(size_t i = 0; i < count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_text(out_json, "{\"sub_category\":");
    json_cstr(out_json, ontology_node(lifts[i].sub_node)->slug);
    buffer_text(out_json, ",\"sup_category\":");
    json_cstr(out_json, ontology_node(lifts[i].sup_node)->slug);
    buffer_text(out_json, ",\"transport_witness\":");
    json_cstr(out_json, lifts[i].transport_witness);
    buffer_format(out_json,
                  ",\"hamming_distance\":%u,\"lawvere_geodesic\":%.4f,\"cartesian_fidelity\":%.4f,"
                  "\"sub_article\":",
                  lifts[i].hamming_distance,
                  lifts[i].lawvere_geodesic,
                  lifts[i].cartesian_fidelity);
    serialize_article(out_json, lifts[i].sub_article, lifts[i].cartesian_fidelity, 0);
    buffer_text(out_json, ",\"sup_article\":");
    serialize_article(out_json, lifts[i].sup_article, lifts[i].cartesian_fidelity, 0);
    buffer_text(out_json, "}");
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_temporal(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  double raw_scores[ONTOLOGY_NODE_COUNT];
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    raw_scores[i] = (double)bitset_popcount(&eng->by_direct_ontology[i]);
  }
  PersistenceBarcode barcode;
  ontology_persistent_homology(raw_scores, &barcode);

  AdjunctionMonadState monad;
  ontology_adjunction_monad(raw_scores, &monad);

  TemporalSheafState temp;
  engine_temporal_sheaf(eng, &temp);

  buffer_format(out_json,
                "{\"persistent_homology\":{\"total_persistence_l1\":%.4f,\"max_void_lifetime\":%.4f,"
                "\"persistent_core\":",
                barcode.total_persistence_l1,
                barcode.max_void_lifetime);
  serialize_ontology_mask_array(out_json, barcode.persistent_core_mask);
  buffer_text(out_json, ",\"filtration_steps\":[");
  for(int s = 0; s < PERSISTENCE_FILTRATION_STEPS; ++s) {
    if(s > 0) buffer_text(out_json, ",");
    buffer_format(out_json,
                  "{\"threshold_eps\":%.2f,\"f_vector\":[%u,%u,%u],"
                  "\"betti_0\":%u,\"betti_1\":%u,\"euler_char\":%d}",
                  barcode.steps[s].threshold_eps,
                  barcode.steps[s].f_vector[0],
                  barcode.steps[s].f_vector[1],
                  barcode.steps[s].f_vector[2],
                  barcode.steps[s].betti_0,
                  barcode.steps[s].betti_1,
                  barcode.steps[s].euler_char);
  }
  buffer_format(out_json,
                "]},\"adjunction_monad\":{\"zig_zag_triangle_defect\":%.6e,"
                "\"beck_coequalizer_defect\":%.6e,\"isbell_adjunction_defect\":%.6e,"
                "\"kleisli_entropy\":%.4f,\"eilenberg_moore_fixed_points\":",
                monad.zig_zag_triangle_defect,
                monad.beck_coequalizer_defect,
                monad.isbell_adjunction_defect,
                monad.kleisli_entropy);
  serialize_ontology_mask_array(out_json, monad.eilenberg_moore_mask);
  buffer_format(out_json,
                "},\"temporal_sheaf\":{\"stalk_count\":%d,\"connection_laplacian_norm\":%.4f,"
                "\"regime_shift_energy\":%.4f,\"fastest_emerging_category\":\"%s\","
                "\"highest_acceleration_category\":\"%s\",\"top_velocities\":{",
                TEMPORAL_STALK_COUNT,
                temp.connection_laplacian_norm,
                temp.regime_shift_energy,
                ontology_node(temp.fastest_emerging_node)->slug,
                ontology_node(temp.highest_acceleration_node)->slug);
  for(uint32_t c = 1u; c < ONTOLOGY_NODE_COUNT; ++c) {
    if(c > 1u) buffer_text(out_json, ",");
    json_cstr(out_json, ontology_node((OntologyNodeId)c)->slug);
    buffer_format(out_json, ":%.2f", temp.velocity[c]);
  }
  buffer_text(out_json, "}}}");
  return 0;
}

static int handle_topos(ServerContext *ctx,
                        const char *path,
                        const char *qs,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char src_r[64], dst_r[64], a_slug[64], b_slug[64];
  if(!query_param(qs, "source_region", src_r, sizeof(src_r)) || !src_r[0]) strcpy(src_r, "east-asia");
  if(!query_param(qs, "target_region", dst_r, sizeof(dst_r)) || !dst_r[0]) strcpy(dst_r, "europe");
  if(!query_param(qs, "a", a_slug, sizeof(a_slug)) || !a_slug[0]) strcpy(a_slug, "ai");
  if(!query_param(qs, "b", b_slug, sizeof(b_slug)) || !b_slug[0]) strcpy(b_slug, "technology");

    WorldRegion sr = REGION_EAST_ASIA, tr = REGION_EUROPE;
    OntologyNodeId na = ONTO_AI, nb = ONTO_TECHNOLOGY;
    world_region_from_slug(src_r, &sr);
    world_region_from_slug(dst_r, &tr);
    ontology_from_slug(a_slug, &na);
    ontology_from_slug(b_slug, &nb);

    ToposState topos;
    engine_topos_state(eng, sr, tr, na, nb, &topos);

    buffer_text(out_json, "{\"source_region\":");
    json_cstr(out_json, world_region_slug(topos.source_region));
    buffer_text(out_json, ",\"target_region\":");
    json_cstr(out_json, world_region_slug(topos.target_region));
    buffer_text(out_json, ",\"proposition_a\":");
    json_cstr(out_json, ontology_node(topos.prop_a)->slug);
    buffer_text(out_json, ",\"proposition_b\":");
    json_cstr(out_json, ontology_node(topos.prop_b)->slug);
    buffer_format(out_json,
                  ",\"bi_heyting_algebra\":{\"leibniz_rule_holds\":%s,\"ideal_a\":",
                  topos.leibniz_rule_holds ? "true" : "false");
    serialize_ontology_mask_array(out_json, topos.ideal_a);
    buffer_text(out_json, ",\"ideal_b\":");
    serialize_ontology_mask_array(out_json, topos.ideal_b);
    buffer_text(out_json, ",\"heyting_implication_ab\":");
    serialize_ontology_mask_array(out_json, topos.heyting_implication_ab);
    buffer_text(out_json, ",\"heyting_negation_a\":");
    serialize_ontology_mask_array(out_json, topos.heyting_negation_a);
    buffer_text(out_json, ",\"lawvere_tierney_sheaf_a\":");
    serialize_ontology_mask_array(out_json, topos.lawvere_tierney_sheaf_a);
    buffer_text(out_json, ",\"coheyting_subtraction_ab\":");
    serialize_ontology_mask_array(out_json, topos.coheyting_subtraction_ab);
    buffer_text(out_json, ",\"coheyting_supplement_a\":");
    serialize_ontology_mask_array(out_json, topos.coheyting_supplement_a);
    buffer_text(out_json, ",\"lawvere_boundary_a\":");
    serialize_ontology_mask_array(out_json, topos.lawvere_boundary_a);
    buffer_text(out_json, ",\"lawvere_boundary_b\":");
    serialize_ontology_mask_array(out_json, topos.lawvere_boundary_b);
    buffer_text(out_json, ",\"lawvere_boundary_meet_ab\":");
    serialize_ontology_mask_array(out_json, topos.lawvere_boundary_meet_ab);
    buffer_text(out_json, ",\"universal_quantifier_ab\":");
    serialize_ontology_mask_array(out_json, topos.universal_quantifier_ab);
    buffer_text(out_json, "},\"equalizer_subobject\":");
    serialize_ontology_mask_array(out_json, topos.equalizer_mask);
    buffer_format(out_json,
                  ",\"isbell_and_enriched_limits\":{\"isbell_envelope_defect\":%.6e,"
                  "\"profunctor_end\":%.4f,\"profunctor_coend\":%.4f,"
                  "\"weighted_colimit\":%.4f,\"weighted_limit\":%.4f,"
                  "\"tensor_hom_adjunction_defect\":%.6e},"
                  "\"subobject_classifier\":{\"score\":",
                  topos.isbell_envelope_defect,
                  topos.profunctor_end,
                  topos.profunctor_coend,
                  topos.weighted_colimit,
                  topos.weighted_limit,
                  topos.tensor_hom_adjunction_defect);
    serialize_ontology_score_map(out_json, topos.classifier_score_a);
    buffer_text(out_json, ",\"characteristic_sieve_a_at_b\":");
    serialize_ontology_mask_array(
        out_json,
        ontology_subobject_classifier_sieve(topos.ideal_a, topos.prop_b));
    buffer_text(out_json, "},\"subobject_classifier_omega\":");
    serialize_ontology_score_map(out_json, topos.classifier_score_a);
    buffer_format(out_json,
                  ",\"cocone_convolution\":{\"cocone_adjunction_defect\":%.6e,"
                  "\"root_convolution\":%.4f,\"root_internal_hom\":%.4f},"
                  "\"day_convolution\":{\"deprecated\":true,\"day_adjunction_defect\":%.6e,"
                  "\"root_convolution\":%.4f,\"root_internal_hom\":%.4f},"
                  "\"modal_closure_diagnostic\":{\"closure_matches\":%s,\"intermediate\":",
                  topos.cocone_adjunction_defect,
                  topos.cocone_convolution_fg[ONTO_ROOT],
                  topos.cocone_internal_hom_gh[ONTO_ROOT],
                  topos.cocone_adjunction_defect,
                  topos.cocone_convolution_fg[ONTO_ROOT],
                  topos.cocone_internal_hom_gh[ONTO_ROOT],
                  topos.modal_closure_matches ? "true" : "false");
    serialize_ontology_mask_array(out_json, topos.modal_intermediate);
    buffer_text(out_json, ",\"target\":");
    serialize_ontology_mask_array(out_json, topos.modal_factorization_target);
    buffer_text(out_json, ",\"diagnostic_only\":true}");
    buffer_format(out_json,
                  ",\"quillen_model_factorization\":{\"deprecated\":true,"
                  "\"weak_equivalence_holds\":%s,\"acyclic_cofibration_m\":",
                  topos.modal_closure_matches ? "true" : "false");
    serialize_ontology_mask_array(out_json, topos.modal_intermediate);
    buffer_text(out_json, ",\"fibration_target\":");
    serialize_ontology_mask_array(out_json, topos.modal_factorization_target);
    buffer_text(out_json, ",\"diagnostic_only\":true");
    buffer_text(out_json, "},\"rank_page_diagnostic\":");
    serialize_rank_page_diagnostic(out_json, &topos.rank_page_diagnostics);
    buffer_format(out_json,
                  ",\"sinkhorn_optimal_transport\":{\"entropic_wasserstein\":%.4f,"
                  "\"sinkhorn_divergence\":%.4f,\"marginal_defect_l1\":%.6e,"
                  "\"coupling_entropy\":%.4f,\"fisher_rao_geodesic\":%.4f,"
                  "\"cheeger_constant\":%.4f,\"cheeger_buser_holds\":%s},"
                  "\"spectral_sequence\":{\"deprecated\":true,\"euler_e0\":%d,\"euler_e1\":%d,"
                  "\"euler_e2\":%d,\"euler_e_inf\":%d,"
                  "\"differential_nilpotency_defect\":%.6e,"
                  "\"e1_stratum_energy\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                  "\"e2_derived_energy\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                  "\"e_inf_abutment_energy\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
                  "\"derived_pages_computed\":false,\"diagnostic_only\":true},"
                  "\"polynomial_functor_codesign\":{\"root_poly_px\":%.4f,\"root_codesign_lens\":%.4f},"
                  "\"ricci_curvature\":{\"gauss_bonnet_total\":%.6f,\"gauss_bonnet_defect\":%.6e,"
                  "\"forman_mean_curvature\":%.4f,\"ollivier_mean_curvature\":%.4f,"
                  "\"ollivier_min_bridge\":%.4f,\"ollivier_max_cluster\":%.4f,"
                  "\"ricci_flow_dirichlet_after\":%.4f,\"ricci_flow_volume_defect\":%.6e},"
                  "\"ihara_zeta_and_tropical\":{\"ihara_u\":%.4f,\"ihara_zeta_val\":%.4f,"
                  "\"ihara_bass_det\":%.6f,\"ihara_det_at_zero_defect\":%.6e,"
                  "\"ihara_det_at_one_defect\":%.6e,\"hashimoto_spectral_radius\":%.4f,"
                  "\"tropical_eigenvalue\":%.4f,\"tropical_idempotence_defect\":%.6e},"
                  "\"rota_baxter_algebra\":{\"rota_baxter_identity_defect\":%.6e,"
                  "\"spitzer_birkhoff_defect\":%.6e}}",
                  topos.sinkhorn_ot.entropic_wasserstein,
                  topos.sinkhorn_ot.sinkhorn_divergence,
                  topos.sinkhorn_ot.marginal_defect_l1,
                  topos.sinkhorn_ot.coupling_entropy,
                  topos.sinkhorn_ot.fisher_rao_geodesic,
                  topos.sinkhorn_ot.cheeger_constant,
                  topos.sinkhorn_ot.cheeger_buser_holds ? "true" : "false",
                  topos.rank_page_diagnostics.euler_e0,
                  topos.rank_page_diagnostics.euler_e1,
                  topos.rank_page_diagnostics.euler_e2,
                  topos.rank_page_diagnostics.euler_e_inf,
                  topos.rank_page_diagnostics.differential_nilpotency_defect,
                  topos.rank_page_diagnostics.e1_stratum_energy[0],
                  topos.rank_page_diagnostics.e1_stratum_energy[1],
                  topos.rank_page_diagnostics.e1_stratum_energy[2],
                  topos.rank_page_diagnostics.e1_stratum_energy[3],
                  topos.rank_page_diagnostics.e1_stratum_energy[4],
                  topos.rank_page_diagnostics.e1_stratum_energy[5],
                  topos.rank_page_diagnostics.e2_derived_energy[0],
                  topos.rank_page_diagnostics.e2_derived_energy[1],
                  topos.rank_page_diagnostics.e2_derived_energy[2],
                  topos.rank_page_diagnostics.e2_derived_energy[3],
                  topos.rank_page_diagnostics.e2_derived_energy[4],
                  topos.rank_page_diagnostics.e2_derived_energy[5],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[0],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[1],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[2],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[3],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[4],
                  topos.rank_page_diagnostics.e_inf_abutment_energy[5],
                  topos.polynomial_functor_px[ONTO_ROOT],
                  topos.polynomial_codesign_lens[ONTO_ROOT],
                  topos.ricci_curv.gauss_bonnet_total,
                  topos.ricci_curv.gauss_bonnet_defect,
                  topos.ricci_curv.forman_mean_curvature,
                  topos.ricci_curv.ollivier_mean_curvature,
                  topos.ricci_curv.ollivier_min_bridge,
                  topos.ricci_curv.ollivier_max_cluster,
                  topos.ricci_curv.ricci_flow_dirichlet_after,
                  topos.ricci_curv.ricci_flow_volume_defect,
                  topos.zeta_tropical.ihara_u,
                  topos.zeta_tropical.ihara_zeta_val,
                  topos.zeta_tropical.ihara_bass_det,
                  topos.zeta_tropical.ihara_det_at_zero_defect,
                  topos.zeta_tropical.ihara_det_at_one_defect,
                  topos.zeta_tropical.hashimoto_spectral_radius,
                  topos.zeta_tropical.tropical_eigenvalue,
                  topos.zeta_tropical.tropical_idempotence_defect,
                  topos.rota_baxter.rota_baxter_identity_defect,
                  topos.rota_baxter.spitzer_birkhoff_defect);
    return 0;
}

static int handle_operad(ServerContext *ctx,
                         const char *path,
                         const char *qs,
                         const char *body,
                         size_t body_len,
                         int *out_status,
                         Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char r1[64], r2[64], r3[64];
  if(!query_param(qs, "r1", r1, sizeof(r1)) || !r1[0]) strcpy(r1, "east-asia");
  if(!query_param(qs, "r2", r2, sizeof(r2)) || !r2[0]) strcpy(r2, "north-america");
  if(!query_param(qs, "r3", r3, sizeof(r3)) || !r3[0]) strcpy(r3, "europe");

  OperadChain chains[16];
  size_t count = engine_operad_compose(eng, r1, r2, r3, chains, 16u);
  buffer_text(out_json, "{\"stage_regions\":[");
  json_cstr(out_json, r1);
  buffer_text(out_json, ",");
  json_cstr(out_json, r2);
  buffer_text(out_json, ",");
  json_cstr(out_json, r3);
  buffer_format(out_json, "],\"total\":%zu,\"operad_chains\":[", count);
  for(size_t i = 0; i < count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_text(out_json, "{\"witness_ab\":");
    json_cstr(out_json, chains[i].witness_ab);
    buffer_text(out_json, ",\"witness_bc\":");
    json_cstr(out_json, chains[i].witness_bc);
    buffer_format(out_json,
                  ",\"total_lawvere_geodesic\":%.4f,\"operad_composition_score\":%.4f,\"composite_topics\":",
                  chains[i].total_lawvere_geodesic,
                  chains[i].operad_composition_score);
    serialize_ontology_mask_array(out_json, chains[i].composite_ontology_mask);
    buffer_text(out_json, ",\"stage_a\":");
    serialize_article(out_json, chains[i].stage_a, chains[i].operad_composition_score, 0);
    buffer_text(out_json, ",\"stage_b\":");
    serialize_article(out_json, chains[i].stage_b, chains[i].operad_composition_score, 0);
    buffer_text(out_json, ",\"stage_c\":");
    serialize_article(out_json, chains[i].stage_c, chains[i].operad_composition_score, 0);
    buffer_text(out_json, "}");
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_feed_xml(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char q_str[128], cat[64], reg[64];
  query_param(qs, "q", q_str, sizeof(q_str));
  query_param(qs, "category", cat, sizeof(cat));
  query_param(qs, "region", reg, sizeof(reg));

  ArticleQuery aq;
  memset(&aq, 0, sizeof(aq));
  aq.query = q_str[0] ? q_str : NULL;
  aq.category = cat[0] ? cat : NULL;
  aq.region = reg[0] ? reg : NULL;
  aq.use_category_morphism = 1;
  aq.limit = 25u;

  SearchResponse res;
  engine_search(eng, &aq, &res);

  buffer_text(out_json,
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
              "  <title>Oyez Category-Theoretic Intelligence Stream</title>\n"
              "  <id>urn:oyez:topos:feed</id>\n"
              "  <updated>2026-11-03T06:00:00Z</updated>\n");
  for(size_t i = 0; i < res.count; ++i) {
    const ArticleRecord *a = res.hits[i].article;
    buffer_format(out_json,
                  "  <entry>\n"
                  "    <id>urn:oyez:article:%u</id>\n"
                  "    <title>",
                  a->id);
    xml_cstr(out_json, a->title ? a->title : "");
    buffer_text(out_json, "</title>\n    <link href=\"");
    xml_cstr(out_json, a->url ? a->url : "");
    buffer_text(out_json, "\"/>\n    <updated>");
    xml_cstr(out_json, (a->published_at && a->published_at[0]) ? a->published_at : "2026-11-03T00:00:00Z");
    buffer_text(out_json, "</updated>\n    <summary>");
    xml_cstr(out_json, a->summary ? a->summary : "");
    buffer_text(out_json, "</summary>\n  </entry>\n");
  }
  buffer_text(out_json, "</feed>\n");
  search_response_free(&res);
  return 0;
}

static int handle_stream(ServerContext *ctx,
                         const char *path,
                         const char *qs,
                         const char *body,
                         size_t body_len,
                         int *out_status,
                         Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  TemporalSheafState temp;
  engine_temporal_sheaf(eng, &temp);
  buffer_format(out_json,
                "event: topology\n"
                "data: {\"feeds\":%zu,\"articles\":%zu,\"leinster_magnitude\":%.4f,"
                "\"connection_laplacian\":%.4f,\"emerging_category\":\"%s\"}\n\n",
                eng->feed_count,
                eng->article_count,
                ontology_leinster_magnitude(),
                temp.connection_laplacian_norm,
                ontology_node(temp.fastest_emerging_node)->slug);
  size_t emit = eng->article_count < 5u ? eng->article_count : 5u;
  for(size_t i = 0; i < emit; ++i) {
    const ArticleRecord *a = &eng->articles[eng->article_count - 1u - i];
    buffer_text(out_json, "event: article\ndata: ");
    serialize_article(out_json, a, 1.0, 0);
    buffer_text(out_json, "\n\n");
  }
  return 0;
}

static int handle_centrality(ServerContext *ctx,
                             const char *path,
                             const char *qs,
                             const char *body,
                             size_t body_len,
                             int *out_status,
                             Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  RankedEntity ents[24];
  ScoredArticle arts[16];
  memset(ents, 0, sizeof(ents));
  memset(arts, 0, sizeof(arts));
  size_t ecount = engine_bipartite_pagerank(eng, 15u, 0.85, ents, 24u, arts, 16u);
  size_t acount = (ecount > 0) ? (eng->article_count < 16u ? eng->article_count : 16u) : 0u;
  buffer_text(out_json, "{\"entities\":[");
  for(size_t i = 0; i < ecount; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_text(out_json, "{\"id\":"); json_cstr(out_json, ents[i].id);
    buffer_text(out_json, ",\"label\":"); json_cstr(out_json, ents[i].label);
    buffer_text(out_json, ",\"kind\":"); json_cstr(out_json, ents[i].kind);
    buffer_format(out_json, ",\"score\":%.6f,\"degree\":%u}", ents[i].score, ents[i].degree);
  }
  buffer_text(out_json, "],\"top_articles\":[");
  size_t emitted = 0;
  for(size_t i = 0; i < acount; ++i) {
    if(!arts[i].article) continue;
    if(emitted++ > 0) buffer_text(out_json, ",");
    serialize_article(out_json, arts[i].article, arts[i].score, 0);
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_catalog(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)ctx; (void)path; (void)body; (void)body_len; (void)out_status;
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

static int handle_catalog_seed(ServerContext *ctx,
                               const char *path,
                               const char *qs,
                               const char *body,
                               size_t body_len,
                               int *out_status,
                               Buffer *out_json) {
  (void)path; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char reg[64], ctry[16];
  query_or_json_param(qs, body, body_len, "region", reg, sizeof(reg));
  query_or_json_param(qs, body, body_len, "country", ctry, sizeof(ctry));
  size_t added = engine_seed_catalog(eng, reg[0] ? reg : NULL, ctry[0] ? ctry : NULL);
  if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
  buffer_format(out_json, "{\"seeded\":%zu,\"total_feeds\":%zu}", added, eng->feed_count);
  return 0;
}

static int handle_feeds(ServerContext *ctx,
                        const char *path,
                        const char *qs,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  buffer_format(out_json, "{\"total\":%zu,\"feeds\":[", eng->feed_count);
  for(size_t i = 0; i < eng->feed_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    serialize_feed(out_json, &eng->feeds[i]);
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_feeds_poll(ServerContext *ctx,
                             const char *path,
                             const char *qs,
                             const char *body,
                             size_t body_len,
                             int *out_status,
                             Buffer *out_json) {
  (void)path; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char lim_s[16];
  query_or_json_param(qs, body, body_len, "limit", lim_s, sizeof(lim_s));
  size_t poll_limit = lim_s[0] ? (size_t)strtoul(lim_s, NULL, 10) : 16u;
  if(poll_limit == 0u) poll_limit = 16u;
  size_t ins = engine_poll_all(eng, poll_limit);
  if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
  buffer_format(out_json, "{\"articles_ingested\":%zu,\"total_articles\":%zu}", ins, eng->article_count);
  return 0;
}

static int handle_markets(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  buffer_text(out_json, "{\"fiscal_stances\":{");
  for(uint32_t fs = 0; fs < FISCAL_STANCE_COUNT; ++fs) {
    if(fs > 0) buffer_text(out_json, ",");
    json_cstr(out_json, fiscal_stance_slug((FiscalStance)fs));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_fiscal_stance[fs]));
  }
  buffer_text(out_json, "},\"assets\":");
  serialize_fiber_counts(out_json, eng->by_asset, 128u);
  buffer_text(out_json, ",\"fiscal_events\":");
  serialize_fiber_counts(out_json, eng->by_fiscal_event, 64u);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_security(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  buffer_text(out_json, "{\"clearance_levels\":{");
  for(uint32_t cl = 0; cl < CLEARANCE_LEVEL_COUNT; ++cl) {
    if(cl > 0) buffer_text(out_json, ",");
    json_cstr(out_json, clearance_level_slug((ClearanceLevel)cl));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_clearance[cl]));
  }
  buffer_text(out_json, "},\"cves\":");
  serialize_fiber_counts(out_json, eng->by_cve, 128u);
  buffer_text(out_json, ",\"organizations\":");
  serialize_fiber_counts(out_json, eng->by_org, 128u);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_ai(ServerContext *ctx,
                     const char *path,
                     const char *qs,
                     const char *body,
                     size_t body_len,
                     int *out_status,
                     Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  buffer_text(out_json, "{\"ai_compute_tiers\":{");
  for(uint32_t at = 0; at < AI_TIER_COUNT; ++at) {
    if(at > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ai_compute_tier_slug((AiComputeTier)at));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_ai_tier[at]));
  }
  buffer_text(out_json, "},\"ai_license_spectrum\":{");
  for(uint32_t al = 0; al < AI_LICENSE_COUNT; ++al) {
    if(al > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ai_weight_license_slug((AiWeightLicense)al));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_ai_license[al]));
  }
  uint32_t closed_cnt = bitset_popcount(&eng->by_ai_license[AI_LICENSE_CLOSED_PROPRIETARY]);
  uint32_t open_cnt = bitset_popcount(&eng->by_ai_license[AI_LICENSE_OPEN_WEIGHTS_RESTRICTED]) +
                      bitset_popcount(&eng->by_ai_license[AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE]) +
                      bitset_popcount(&eng->by_ai_license[AI_LICENSE_OPEN_SCIENCE_OSI]);
  double open_ratio = (open_cnt + closed_cnt) > 0
                          ? ((double)open_cnt / (double)(open_cnt + closed_cnt))
                          : 0.0;
  buffer_format(out_json,
                "},\"open_vs_closed_summary\":{\"open_ecosystem_articles\":%u,"
                "\"closed_frontier_articles\":%u,\"openness_ratio\":%.4f},\"ai_models\":",
                open_cnt,
                closed_cnt,
                open_ratio);
  serialize_fiber_counts(out_json, eng->by_ai_model, 128u);
  buffer_text(out_json, ",\"ai_infrastructure\":");
  serialize_fiber_counts(out_json, eng->by_ai_infra, 128u);
  buffer_text(out_json, ",\"huggingface_repos\":");
  serialize_fiber_counts(out_json, eng->by_hf_repo, 128u);
  buffer_text(out_json, ",\"top_ai_articles\":");
  serialize_top_articles_by_score(out_json, eng, article_ai_score, 12u);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_sports(ServerContext *ctx,
                         const char *path,
                         const char *qs,
                         const char *body,
                         size_t body_len,
                         int *out_status,
                         Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  uint32_t sp_total = bitset_popcount(&eng->by_closure_ontology[ONTO_SPORTS]);
  buffer_format(out_json, "{\"sports_articles_count\":%u,\"sports_leagues\":", sp_total);
  serialize_fiber_counts(out_json, eng->by_sport, 128u);
  buffer_text(out_json, ",\"top_sports_articles\":");
  serialize_top_articles_by_score(out_json, eng, article_sports_score, 12u);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_weather(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char t_str[32], rh_str[32], lat_str[32], u_str[32], f_str[32];
  query_param(qs, "temp_c", t_str, sizeof(t_str));
  query_param(qs, "humidity", rh_str, sizeof(rh_str));
  query_param(qs, "lat_deg", lat_str, sizeof(lat_str));
  query_param(qs, "u_wind", u_str, sizeof(u_str));
  query_param(qs, "forcing", f_str, sizeof(f_str));

  uint32_t wx_total = bitset_popcount(&eng->by_closure_ontology[ONTO_CLIMATE]);
  uint32_t severe_cnt = bitset_popcount(&eng->by_weather_severity[WEATHER_WATCH]) +
                        bitset_popcount(&eng->by_weather_severity[WEATHER_WARNING]) +
                        bitset_popcount(&eng->by_weather_severity[WEATHER_CATASTROPHIC]);

  double sum_wx_score = 0.0;
  size_t wx_scored = 0;
  for(size_t i = 0; i < eng->article_count; ++i) {
    if(eng->articles[i].weather_score > 0.0) {
      sum_wx_score += eng->articles[i].weather_score;
      wx_scored++;
    }
  }
  double avg_wx_score = wx_scored > 0 ? (sum_wx_score / (double)wx_scored) : 2.5;

  double temp_c = t_str[0] ? strtod(t_str, NULL) : (24.0 + 0.15 * (double)severe_cnt);
  double rh_pct = rh_str[0] ? strtod(rh_str, NULL) : 68.0;
  double u_wind = u_str[0] ? strtod(u_str, NULL) : 18.0;
  double lat_deg = lat_str[0] ? strtod(lat_str, NULL) : 38.0;
  double forcing = f_str[0] ? strtod(f_str, NULL) : avg_wx_score;

  WeatherPhysicsState phys;
  ontology_atmospheric_dynamics(temp_c, rh_pct, u_wind, lat_deg, forcing, &phys);

  buffer_format(out_json,
                "{\"weather_articles_count\":%u,\"severe_hazards_count\":%u,\"weather_severities\":{",
                wx_total, severe_cnt);
  for(uint32_t ws = 0; ws < WEATHER_SEVERITY_COUNT; ++ws) {
    if(ws > 0) buffer_text(out_json, ",");
    json_cstr(out_json, weather_severity_slug((WeatherSeverity)ws));
    buffer_format(out_json, ":%u", bitset_popcount(&eng->by_weather_severity[ws]));
  }
  buffer_text(out_json, "},\"weather_phenomena\":");
  serialize_fiber_counts(out_json, eng->by_weather, 128u);
  buffer_format(out_json,
                ",\"atmospheric_physics\":{"
                "\"lorenz_63_attractor\":[%.6f,%.6f,%.6f],"
                "\"maximal_lyapunov_exponent\":%.6f,"
                "\"liouville_divergence\":%.6f,"
                "\"kaplan_yorke_dimension\":%.6f,"
                "\"predictability_horizon_units\":%.6f,"
                "\"saturation_vapor_pressure_hpa\":%.4f,"
                "\"wet_bulb_temperature_c\":%.4f,"
                "\"rossby_phase_speed_ms\":%.4f,"
                "\"rossby_stationary_wavenumber\":%.4f,"
                "\"omega_blocking_regime\":%s"
                "},\"top_weather_articles\":",
                phys.lorenz_state[0],
                phys.lorenz_state[1],
                phys.lorenz_state[2],
                phys.lyapunov_max,
                phys.lyapunov_sum_div,
                phys.kaplan_yorke_dim,
                phys.predictability_horizon,
                phys.saturation_vapor_hpa,
                phys.wet_bulb_celsius,
                phys.rossby_phase_speed,
                phys.rossby_stationary_k,
                phys.omega_blocking_regime ? "true" : "false");
  serialize_top_articles_by_score(out_json, eng, article_weather_score, 12u);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_automation_get(ServerContext *ctx,
                                 const char *path,
                                 const char *qs,
                                 const char *body,
                                 size_t body_len,
                                 int *out_status,
                                 Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  buffer_format(out_json,
                "{\"auto_poll_interval_sec\":%u,\"auto_cycles_completed\":%llu,"
                "\"last_auto_cycle_ms\":%llu,\"total_bandit_pulls\":%llu,"
                "\"rule_count\":%zu,\"alert_count\":%zu,\"rules\":[",
                eng->auto_poll_interval_sec,
                (unsigned long long)eng->auto_cycles_completed,
                (unsigned long long)eng->last_auto_cycle_ms,
                (unsigned long long)eng->total_bandit_pulls,
                eng->automation_rule_count,
                eng->automation_alert_count);
  for(size_t i = 0; i < eng->automation_rule_count; ++i) {
    const AutomationRule *r = &eng->automation_rules[i];
    if(i > 0) buffer_text(out_json, ",");
    buffer_format(out_json, "{\"id\":%u,\"enabled\":%s,\"name\":",
                  r->id, r->enabled ? "true" : "false");
    json_cstr(out_json, r->name);
    buffer_text(out_json, ",\"query\":"); json_cstr(out_json, r->query);
    buffer_text(out_json, ",\"category\":"); json_cstr(out_json, r->category);
    buffer_text(out_json, ",\"region\":"); json_cstr(out_json, r->region);
    buffer_text(out_json, ",\"weather\":"); json_cstr(out_json, r->weather);
    buffer_text(out_json, ",\"min_clearance\":"); json_cstr(out_json, clearance_level_slug(r->min_clearance));
    buffer_text(out_json, ",\"fiscal_stance\":"); json_cstr(out_json, fiscal_stance_slug(r->fiscal_stance));
    buffer_text(out_json, ",\"min_ai_tier\":"); json_cstr(out_json, ai_compute_tier_slug(r->min_ai_tier));
    buffer_text(out_json, ",\"min_weather_severity\":"); json_cstr(out_json, weather_severity_slug(r->min_weather_severity));
    buffer_format(out_json, ",\"min_score\":%.2f,\"action\":", r->min_score);
    json_cstr(out_json, automation_action_slug(r->action));
    buffer_text(out_json, ",\"target\":"); json_cstr(out_json, r->target);
    buffer_format(out_json,
                  ",\"hit_count\":%u,\"last_triggered_ms\":%llu}",
                  r->hit_count, (unsigned long long)r->last_triggered_ms);
  }
  buffer_text(out_json, "],\"alerts\":[");
  size_t emit_alerts = eng->automation_alert_count < 32u ? eng->automation_alert_count : 32u;
  for(size_t k = 0; k < emit_alerts; ++k) {
    size_t idx = (eng->automation_alert_head + MAX_AUTOMATION_ALERTS - 1u - k) % MAX_AUTOMATION_ALERTS;
    const AutomationAlert *al = &eng->automation_alerts[idx];
    if(k > 0) buffer_text(out_json, ",");
    buffer_format(out_json,
                  "{\"alert_id\":%llu,\"rule_id\":%u,\"article_id\":%u,"
                  "\"triggered_at_ms\":%llu,\"trigger_score\":%.4f,\"action\":",
                  (unsigned long long)al->alert_id, al->rule_id, al->article_id,
                  (unsigned long long)al->triggered_at_ms, al->trigger_score);
    json_cstr(out_json, automation_action_slug(al->action_taken));
    buffer_text(out_json, ",\"rule_name\":"); json_cstr(out_json, al->rule_name);
    buffer_text(out_json, ",\"article_title\":"); json_cstr(out_json, al->article_title);
    buffer_text(out_json, ",\"article_url\":"); json_cstr(out_json, al->article_url);
    buffer_text(out_json, ",\"category\":"); json_cstr(out_json, al->category_slug);
    buffer_text(out_json, ",\"weather_severity\":"); json_cstr(out_json, al->weather_severity_slug);
    buffer_text(out_json, ",\"clearance\":"); json_cstr(out_json, al->clearance_slug);
    buffer_text(out_json, "}");
  }
  buffer_text(out_json, "],\"bandit_schedule\":[");
  BanditFeedPriority prio[16];
  size_t bcount = engine_bandit_select_feeds(eng, prio, 16u);
  for(size_t i = 0; i < bcount; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    buffer_format(out_json, "{\"feed_id\":%u,\"title\":", prio[i].feed_id);
    json_cstr(out_json, prio[i].title);
    buffer_text(out_json, ",\"category\":"); json_cstr(out_json, prio[i].category);
    buffer_format(out_json,
                  ",\"ucb1_score\":%.4f,\"empirical_yield\":%.4f,"
                  "\"exploration_bonus\":%.4f,\"staleness_bonus\":%.4f,\"error_penalty\":%.4f}",
                  prio[i].ucb1_score, prio[i].empirical_yield,
                  prio[i].exploration_bonus, prio[i].staleness_bonus, prio[i].error_penalty);
  }
  buffer_text(out_json, "]}");
  return 0;
}

static int handle_automation_rules_post(ServerContext *ctx,
                                        const char *path,
                                        const char *qs,
                                        const char *body,
                                        size_t body_len,
                                        int *out_status,
                                        Buffer *out_json) {
  (void)path; (void)qs;
  OyezEngine *eng = ctx->engine;
  char id_s[16], en_s[16], name_s[64], q_s[64], cat_s[32], reg_s[32], wx_s[32];
  char clr_s[24], fst_s[24], ait_s[24], wxs_s[24], sc_s[24], act_s[32], tgt_s[128];
  json_extract_field(body, body_len, "id", id_s, sizeof(id_s));
  json_extract_field(body, body_len, "enabled", en_s, sizeof(en_s));
  json_extract_field(body, body_len, "name", name_s, sizeof(name_s));
  json_extract_field(body, body_len, "query", q_s, sizeof(q_s));
  if(q_s[0] == '\0') json_extract_field(body, body_len, "query_terms", q_s, sizeof(q_s));
  json_extract_field(body, body_len, "category", cat_s, sizeof(cat_s));
  json_extract_field(body, body_len, "region", reg_s, sizeof(reg_s));
  json_extract_field(body, body_len, "weather", wx_s, sizeof(wx_s));
  json_extract_field(body, body_len, "min_clearance", clr_s, sizeof(clr_s));
  json_extract_field(body, body_len, "fiscal_stance", fst_s, sizeof(fst_s));
  json_extract_field(body, body_len, "min_ai_tier", ait_s, sizeof(ait_s));
  json_extract_field(body, body_len, "min_weather_severity", wxs_s, sizeof(wxs_s));
  json_extract_field(body, body_len, "min_score", sc_s, sizeof(sc_s));
  json_extract_field(body, body_len, "action", act_s, sizeof(act_s));
  json_extract_field(body, body_len, "target", tgt_s, sizeof(tgt_s));

  if(name_s[0] == '\0') {
    *out_status = 400;
    buffer_text(out_json, "{\"error\":\"missing_rule_name\"}");
    return 0;
  }

  AutomationRule rule;
  memset(&rule, 0, sizeof(rule));
  rule.id = id_s[0] ? (uint32_t)strtoul(id_s, NULL, 10) : 0u;
  rule.enabled = (strcmp(en_s, "0") == 0 || strcmp(en_s, "false") == 0) ? 0 : 1;
  snprintf(rule.name, sizeof(rule.name), "%s", name_s);
  snprintf(rule.query, sizeof(rule.query), "%s", q_s);
  snprintf(rule.category, sizeof(rule.category), "%s", cat_s);
  snprintf(rule.region, sizeof(rule.region), "%s", reg_s);
  snprintf(rule.weather, sizeof(rule.weather), "%s", wx_s);
  if(clr_s[0] != '\0') clearance_level_from_slug(clr_s, &rule.min_clearance);
  if(fst_s[0] != '\0') fiscal_stance_from_slug(fst_s, &rule.fiscal_stance);
  if(ait_s[0] != '\0') ai_compute_tier_from_slug(ait_s, &rule.min_ai_tier);
  if(wxs_s[0] != '\0') weather_severity_from_slug(wxs_s, &rule.min_weather_severity);
  rule.min_score = sc_s[0] ? strtod(sc_s, NULL) : 0.0;
  AutomationActionType act = AUTO_ACTION_ALERT_RING;
  if(act_s[0] != '\0') automation_action_from_slug(act_s, &act);
  rule.action = act;
  snprintf(rule.target, sizeof(rule.target), "%s", tgt_s);

  uint32_t rule_id = 0;
  if(engine_upsert_automation_rule(eng, &rule, &rule_id) != 0) {
    *out_status = 400;
    buffer_text(out_json, "{\"error\":\"rule_capacity_exceeded\"}");
    return 0;
  }
  size_t triggered = engine_evaluate_all_automation_rules(eng);
  if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
  buffer_format(out_json,
                "{\"status\":\"ok\",\"rule_id\":%u,\"rule_count\":%zu,\"alerts_triggered\":%zu}",
                rule_id, eng->automation_rule_count, triggered);
  return 0;
}

static int handle_automation_rules_delete(ServerContext *ctx,
                                          const char *path,
                                          const char *qs,
                                          const char *body,
                                          size_t body_len,
                                          int *out_status,
                                          Buffer *out_json) {
  (void)path;
  OyezEngine *eng = ctx->engine;
  char id_s[16];
  query_or_json_param(qs, body, body_len, "id", id_s, sizeof(id_s));
  uint32_t rule_id = id_s[0] ? (uint32_t)strtoul(id_s, NULL, 10) : 0u;
  if(rule_id == 0u || engine_delete_automation_rule(eng, rule_id) != 0) {
    *out_status = 404;
    buffer_text(out_json, "{\"error\":\"rule_not_found\"}");
    return 0;
  }
  if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
  buffer_format(out_json, "{\"status\":\"deleted\",\"rule_id\":%u,\"rule_count\":%zu}",
                rule_id, eng->automation_rule_count);
  return 0;
}

static int handle_automation_run(ServerContext *ctx,
                                 const char *path,
                                 const char *qs,
                                 const char *body,
                                 size_t body_len,
                                 int *out_status,
                                 Buffer *out_json) {
  (void)path; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char batch_s[16], intv_s[16];
  query_or_json_param(qs, body, body_len, "poll_batch", batch_s, sizeof(batch_s));
  query_or_json_param(qs, body, body_len, "auto_poll_interval_sec", intv_s, sizeof(intv_s));
  if(intv_s[0] != '\0') {
    eng->auto_poll_interval_sec = (uint32_t)strtoul(intv_s, NULL, 10);
  }
  size_t poll_batch = batch_s[0] ? (size_t)strtoul(batch_s, NULL, 10) : 0u;
  size_t before_alerts = eng->automation_alert_count;
  size_t ingested = 0;
  if(poll_batch > 0u) {
    ingested = engine_poll_bandit_auto(eng, poll_batch, 4u);
  } else {
    engine_evaluate_all_automation_rules(eng);
    eng->last_auto_cycle_ms = wall_time_ms();
    eng->auto_cycles_completed++;
  }
  size_t new_alerts = eng->automation_alert_count >= before_alerts
                          ? (eng->automation_alert_count - before_alerts)
                          : 0u;
  if(ctx->snapshot_path) engine_save_snapshot(eng, ctx->snapshot_path);
  buffer_format(out_json,
                "{\"status\":\"ok\",\"auto_cycles_completed\":%llu,\"articles_ingested\":%zu,"
                "\"alerts_triggered\":%zu,\"total_alerts\":%zu}",
                (unsigned long long)eng->auto_cycles_completed, ingested, new_alerts, eng->automation_alert_count);
  return 0;
}

static int handle_appliance(ServerContext *ctx,
                            const char *path,
                            const char *qs,
                            const char *body,
                            size_t body_len,
                            int *out_status,
                            Buffer *out_json) {
  (void)path; (void)qs; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  EngineApplianceState app;
  engine_appliance_state(eng, &app);
  size_t cat_count = 0;
  catalog_entries(&cat_count);

    buffer_format(out_json,
                  "{\"appliance\":\"oyez-one-machine-c99\",\"status\":\"%s\","
                  "\"status_deprecated\":true,"
                  "\"catalog_sources\":%zu,\"indexed_feeds\":%zu,\"indexed_articles\":%zu,\"inverted_terms\":%zu,"
                  "\"index_generation\":%llu,\"m1_tokenizer_vocab_size\":%u,\"m1_tokenizer_sha256\":",
                  app.all_checks_passed ? "verified" : "degraded",
                  cat_count, eng->feed_count, eng->article_count, eng->inv_count,
                  (unsigned long long)app.index_generation,
                  ctx->tokenizer ? m1_tokenizer_vocab_size(ctx->tokenizer) : 0u);
    json_cstr(out_json, ctx->tokenizer ? ctx->tokenizer->sha256 : "");
    buffer_format(out_json,
                  ",\"consistency_status\":\"%s\",\"all_checks_passed\":%s,"
                  "\"checks_passed\":%u,\"checks_total\":%u,"
                  "\"all_theorems_verified\":%s,\"theorems_verified_count\":%u,\"theorems_total_count\":%u,"
                  "\"memory\":{\"total_resident_bytes\":%zu,\"struct_base_bytes\":%zu,"
                  "\"arena_bytes_used\":%zu,\"arena_bytes_capacity\":%zu,\"arena_block_count\":%zu,"
                  "\"feed_and_article_table_bytes\":%zu,\"inverted_index_bytes\":%zu,"
                  "\"bitset_fiber_bytes\":%zu,\"lsh_and_dedup_bytes\":%zu,"
                  "\"query_cache_bytes\":%zu,\"query_cache_valid_entries\":%zu},"
                  "\"mathematical_invariants\":{\"leinster_magnitude\":%.8f,\"euler_characteristic\":%d,"
                  "\"fiedler_eigenvalue\":%.6f,\"cheeger_constant\":%.6f,\"gauss_bonnet_total\":%.8f,"
                  "\"ihara_zeta_val\":%.6f,\"tropical_eigenvalue\":%.6f,\"sinkhorn_divergence\":%.6f,"
                  "\"fisher_rao_geodesic\":%.6f,\"cocone_adjunction_defect\":%.12e,"
                  "\"day_adjunction_defect\":%.12e,"
                  "\"rota_baxter_defect\":%.12e,\"max_numerical_residual\":%.12e,"
                  "\"max_numerical_defect\":%.12e},"
                  "\"atmospheric_physics\":{\"lorenz_state\":[%.4f,%.4f,%.4f],\"lyapunov_max\":%.6f,"
                  "\"lyapunov_sum_div\":%.4f,\"kaplan_yorke_dim\":%.6f,\"predictability_horizon\":%.4f,"
                  "\"saturation_vapor_hpa\":%.4f,\"wet_bulb_celsius\":%.4f,\"rossby_phase_speed\":%.4f,"
                  "\"rossby_stationary_k\":%.4f,\"omega_blocking_regime\":%s},"
                  "\"situation_room\":{\"cyber_strategic_articles\":%zu,\"frontier_ai_articles\":%zu,"
                  "\"severe_weather_articles\":%zu,\"macro_active_articles\":%zu,\"sports_articles\":%zu,"
                  "\"emerging_category\":",
                  app.all_checks_passed ? "passing" : "degraded",
                  app.all_checks_passed ? "true" : "false",
                  app.checks_passed,
                  app.checks_total,
                  app.all_checks_passed ? "true" : "false",
                  app.checks_passed,
                  app.checks_total,
                  app.total_resident_bytes,
                  app.struct_base_bytes,
                  app.arena_bytes_used,
                  app.arena_bytes_capacity,
                  app.arena_block_count,
                  app.feed_and_article_table_bytes,
                  app.inverted_index_bytes,
                  app.bitset_fiber_bytes,
                  app.lsh_and_dedup_bytes,
                  app.query_cache_bytes,
                  app.query_cache_valid_entries,
                  app.leinster_magnitude,
                  app.euler_characteristic,
                  app.fiedler_eigenvalue,
                  app.cheeger_constant,
                  app.gauss_bonnet_total,
                  app.ihara_zeta_val,
                  app.tropical_eigenvalue,
                  app.sinkhorn_divergence,
                  app.fisher_rao_geodesic,
                  app.cocone_adjunction_defect,
                  app.cocone_adjunction_defect,
                  app.rota_baxter_defect,
                  app.max_numerical_residual,
                  app.max_numerical_residual,
                  app.weather_physics.lorenz_state[0],
                  app.weather_physics.lorenz_state[1],
                  app.weather_physics.lorenz_state[2],
                  app.weather_physics.lyapunov_max,
                  app.weather_physics.lyapunov_sum_div,
                  app.weather_physics.kaplan_yorke_dim,
                  app.weather_physics.predictability_horizon,
                  app.weather_physics.saturation_vapor_hpa,
                  app.weather_physics.wet_bulb_celsius,
                  app.weather_physics.rossby_phase_speed,
                  app.weather_physics.rossby_stationary_k,
                  app.weather_physics.omega_blocking_regime ? "true" : "false",
                  app.cyber_strategic_articles,
                  app.frontier_ai_articles,
                  app.severe_weather_articles,
                  app.macro_active_articles,
                  app.sports_articles);
    json_cstr(out_json, app.emerging_category_slug);
    buffer_format(out_json,
                  ",\"emerging_acceleration_z\":%.4f},"
                  "\"automation\":{\"active_watchpoint_rules\":%zu,\"total_watchpoint_alerts\":%zu,"
                  "\"total_bandit_pulls\":%llu,\"auto_poll_interval_sec\":%u,\"auto_cycles_completed\":%u,"
                  "\"top_bandit_feeds\":[",
                  app.emerging_acceleration_z,
                  app.active_watchpoint_rules,
                  app.total_watchpoint_alerts,
                  (unsigned long long)app.total_bandit_pulls,
                  app.auto_poll_interval_sec,
                  app.auto_cycles_completed);
    for(size_t i = 0; i < app.top_bandit_count; ++i) {
      if(i > 0) buffer_text(out_json, ",");
      buffer_format(out_json, "{\"feed_id\":%u,\"title\":", app.top_bandit_feeds[i].feed_id);
      json_cstr(out_json, app.top_bandit_feeds[i].title ? app.top_bandit_feeds[i].title : "");
      buffer_text(out_json, ",\"category\":");
      json_cstr(out_json, app.top_bandit_feeds[i].category ? app.top_bandit_feeds[i].category : "");
      buffer_format(out_json, ",\"ucb1_score\":%.4f}", app.top_bandit_feeds[i].ucb1_score);
    }
    buffer_text(out_json, "]}}");
    return 0;
}

static int handle_articles_search(ServerContext *ctx,
                                  const char *path,
                                  const char *qs,
                                  const char *body,
                                  size_t body_len,
                                  int *out_status,
                                  Buffer *out_json) {
  (void)path; (void)body; (void)body_len; (void)out_status;
  OyezEngine *eng = ctx->engine;
  char q_str[256], cat[64], ex_cat[64], morph[16], ntype[64], clr[64], fst[64], asset[32], fev[32];
  char cve_str[32], org_str[32], ait_str[64], aim_str[32], aii_str[32], ail_str[64], hfr_str[64], sp_str[32];
  char wx_str[32], wxs_str[32];
  char reg[64], ctry[16], mctry[16], lang[16], tier[32];
  char div_str[16], lam_str[16], lim_str[16], off_str[16], fid_str[16];
  query_param(qs, "q", q_str, sizeof(q_str));
  query_param(qs, "category", cat, sizeof(cat));
  query_param(qs, "exclude_category", ex_cat, sizeof(ex_cat));
  query_param(qs, "category_morphism", morph, sizeof(morph));
  query_param(qs, "news_type", ntype, sizeof(ntype));
  query_param(qs, "clearance", clr, sizeof(clr));
  query_param(qs, "fiscal_stance", fst, sizeof(fst));
  query_param(qs, "asset", asset, sizeof(asset));
  query_param(qs, "fiscal_event", fev, sizeof(fev));
  query_param(qs, "cve", cve_str, sizeof(cve_str));
  query_param(qs, "organization", org_str, sizeof(org_str));
  query_param(qs, "ai_tier", ait_str, sizeof(ait_str));
  query_param(qs, "ai_model", aim_str, sizeof(aim_str));
  query_param(qs, "ai_infra", aii_str, sizeof(aii_str));
  query_param(qs, "ai_license", ail_str, sizeof(ail_str));
  query_param(qs, "hf_repo", hfr_str, sizeof(hfr_str));
  query_param(qs, "sport", sp_str, sizeof(sp_str));
  query_param(qs, "weather", wx_str, sizeof(wx_str));
  query_param(qs, "weather_severity", wxs_str, sizeof(wxs_str));
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
  article_query_init(&aq);
  aq.query = q_str[0] ? q_str : NULL;
  aq.category = cat[0] ? cat : NULL;
  aq.exclude_category = ex_cat[0] ? ex_cat : NULL;
  aq.use_category_morphism = (strcmp(morph, "0") != 0 && strcmp(morph, "false") != 0);
  aq.news_type = ntype[0] ? ntype : NULL;
  aq.clearance = clr[0] ? clr : NULL;
  aq.fiscal_stance = fst[0] ? fst : NULL;
  aq.asset = asset[0] ? asset : NULL;
  aq.fiscal_event = fev[0] ? fev : NULL;
  aq.cve = cve_str[0] ? cve_str : NULL;
  aq.organization = org_str[0] ? org_str : NULL;
  aq.ai_tier = ait_str[0] ? ait_str : NULL;
  aq.ai_model = aim_str[0] ? aim_str : NULL;
  aq.ai_infra = aii_str[0] ? aii_str : NULL;
  aq.ai_license = ail_str[0] ? ail_str : NULL;
  aq.hf_repo = hfr_str[0] ? hfr_str : NULL;
  aq.sport = sp_str[0] ? sp_str : NULL;
  aq.weather = wx_str[0] ? wx_str : NULL;
  aq.weather_severity = wxs_str[0] ? wxs_str : NULL;
  aq.region = reg[0] ? reg : NULL;
  aq.country = ctry[0] ? ctry : NULL;
  aq.mentioned_country = mctry[0] ? mctry : NULL;
  aq.language = lang[0] ? lang : NULL;
  aq.source_tier = tier[0] ? tier : NULL;
  aq.feed_id = fid_str[0] ? (uint32_t)strtoul(fid_str, NULL, 10) : 0u;
  aq.diversify_mmr = (strcmp(div_str, "2") == 0 || strcmp(div_str, "dpp") == 0)
                         ? 2
                         : ((strcmp(div_str, "1") == 0 || strcmp(div_str, "true") == 0) ? 1 : 0);
  if(lam_str[0]) aq.mmr_lambda = strtod(lam_str, NULL);
  aq.limit = lim_str[0] ? (size_t)strtoul(lim_str, NULL, 10) : 50u;
  aq.offset = off_str[0] ? (size_t)strtoul(off_str, NULL, 10) : 0u;

  SearchResponse res;
  engine_search(eng, &aq, &res);
  buffer_format(out_json,
                "{\"total\":%zu,\"count\":%zu,\"query_time_ms\":%.4f,\"articles\":[",
                res.total, res.count, res.query_time_ms);
  for(size_t i = 0; i < res.count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    serialize_article_with_query(out_json, res.hits[i].article, res.hits[i].score, 0, aq.query);
  }
  buffer_text(out_json, "]}");
  search_response_free(&res);
  return 0;
}

static int handle_article_by_id(ServerContext *ctx,
                                const char *path,
                                const char *qs,
                                const char *body,
                                size_t body_len,
                                int *out_status,
                                Buffer *out_json) {
  (void)qs; (void)body; (void)body_len;
  OyezEngine *eng = ctx->engine;
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

static int handle_article_enrich(ServerContext *ctx,
                                 const char *path,
                                 const char *qs,
                                 const char *body,
                                 size_t body_len,
                                 int *out_status,
                                 Buffer *out_json) {
  (void)qs;
  if(strstr(path + 17u, "/enrich") == NULL) {
    *out_status = 404;
    buffer_text(out_json, "{\"error\":\"not_found\"}");
    return 0;
  }
  OyezEngine *eng = ctx->engine;
  uint32_t id = (uint32_t)strtoul(path + 17u, NULL, 10);
  const ArticleRecord *a = engine_get_article_by_id(eng, id);
  if(!a) {
    *out_status = 404;
    buffer_text(out_json, "{\"error\":\"article_not_found\"}");
    return 0;
  }
  ExtractedContent ex;
  if(extract_html_from_request_or_url(body, body_len, a->url, &ex, out_status, out_json,
                                      "enrichment_source_unavailable") != 0) {
    return 0;
  }
  int upgraded = engine_enrich_article(eng, id, ex.byline, ex.excerpt, ex.markdown,
                                       ex.text_content, ex.lead_image_url);
  readability_free(&ex);

  const ArticleRecord *updated = engine_get_article_by_id(eng, id);
  buffer_format(out_json, "{\"status\":\"enriched\",\"upgraded\":%s,\"article\":",
                upgraded > 0 ? "true" : "false");
  serialize_article(out_json, updated, 1.0, 1);
  buffer_text(out_json, "}");
  return 0;
}

static int handle_extract(ServerContext *ctx,
                          const char *path,
                          const char *qs,
                          const char *body,
                          size_t body_len,
                          int *out_status,
                          Buffer *out_json) {
  (void)path; (void)qs;
  OyezEngine *eng = ctx->engine;
  char aid_str[16];
  json_extract_field(body, body_len, "article_id", aid_str, sizeof(aid_str));
  ExtractedContent ex;
  if(extract_html_from_request_or_url(body, body_len, NULL, &ex, out_status, out_json,
                                      "missing_html_or_url") != 0) {
    return 0;
  }
  if(aid_str[0] != '\0') {
    uint32_t aid = (uint32_t)strtoul(aid_str, NULL, 10);
    if(aid > 0u) {
      engine_enrich_article(eng, aid, ex.byline, ex.excerpt, ex.markdown,
                            ex.text_content, ex.lead_image_url);
    }
  }
  ExtractedEntities ent;
  entities_extract(ex.title, ex.excerpt, ex.text_content, NULL, NULL, 0, &ent);

  buffer_text(out_json, "{\"extracted\":{\"title\":"); json_cstr(out_json, ex.title);
  buffer_text(out_json, ",\"byline\":"); json_cstr(out_json, ex.byline);
  buffer_text(out_json, ",\"excerpt\":"); json_cstr(out_json, ex.excerpt);
  buffer_text(out_json, ",\"site_name\":"); json_cstr(out_json, ex.site_name);
  buffer_text(out_json, ",\"published_time\":"); json_cstr(out_json, ex.published_time);
  buffer_text(out_json, ",\"lead_image_url\":"); json_cstr(out_json, ex.lead_image_url);
  buffer_text(out_json, ",\"text_content\":"); json_cstr(out_json, ex.text_content);
  buffer_text(out_json, ",\"markdown\":"); json_cstr(out_json, ex.markdown);
  buffer_format(out_json, ",\"word_count\":%u,\"reading_time_minutes\":%u},\"entities\":{\"news_type\":",
                ex.word_count, ex.reading_time_minutes);
  json_cstr(out_json, news_type_slug(ent.news_type));
  buffer_text(out_json, ",\"clearance\":");
  json_cstr(out_json, clearance_level_slug(ent.clearance));
  buffer_format(out_json, ",\"opsec_score\":%.2f,\"fiscal_stance\":", ent.opsec_score);
  json_cstr(out_json, fiscal_stance_slug(ent.fiscal_stance));
  buffer_format(out_json, ",\"macro_score\":%.2f,\"topics\":", ent.macro_score);
  serialize_ontology_mask_array(out_json, ent.direct_ontology_mask);
  buffer_text(out_json, ",\"category_closure\":");
  serialize_ontology_mask_array(out_json, ent.closure_ontology_mask);
  buffer_text(out_json, ",\"countries\":[");
  for(size_t i = 0; i < ent.country_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.countries[i]);
  }
  buffer_text(out_json, "],\"organizations\":[");
  for(size_t i = 0; i < ent.org_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.organizations[i]);
  }
  buffer_text(out_json, "],\"cves\":[");
  for(size_t i = 0; i < ent.cve_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.cves[i]);
  }
  buffer_text(out_json, "],\"assets\":[");
  for(size_t i = 0; i < ent.asset_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.assets[i]);
  }
  buffer_text(out_json, "],\"fiscal_events\":[");
  for(size_t i = 0; i < ent.fiscal_event_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.fiscal_events[i]);
  }
  buffer_text(out_json, "],\"ai_tier\":");
  json_cstr(out_json, ai_compute_tier_slug(ent.ai_tier));
  buffer_format(out_json, ",\"ai_compute_score\":%.2f,\"ai_license\":", ent.ai_compute_score);
  json_cstr(out_json, ai_weight_license_slug(ent.ai_license));
  buffer_format(out_json, ",\"openness_score\":%.2f,\"ai_models\":[", ent.openness_score);
  for(size_t i = 0; i < ent.ai_model_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.ai_models[i]);
  }
  buffer_text(out_json, "],\"ai_infra\":[");
  for(size_t i = 0; i < ent.ai_infra_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.ai_infra[i]);
  }
  buffer_text(out_json, "],\"hf_repos\":[");
  for(size_t i = 0; i < ent.hf_repo_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.hf_repos[i]);
  }
  buffer_format(out_json, "],\"sports_score\":%.2f,\"sports\":[", ent.sports_score);
  for(size_t i = 0; i < ent.sport_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.sports[i]);
  }
  buffer_text(out_json, "],\"weather_severity\":");
  json_cstr(out_json, weather_severity_slug(ent.weather_severity));
  buffer_format(out_json, ",\"weather_score\":%.2f,\"weather\":[", ent.weather_score);
  for(size_t i = 0; i < ent.weather_count; ++i) {
    if(i > 0) buffer_text(out_json, ",");
    json_cstr(out_json, ent.weather[i]);
  }
  buffer_text(out_json, "]}}");

  readability_free(&ex);
  return 0;
}

static int handle_tokenize(ServerContext *ctx,
                           const char *path,
                           const char *qs,
                           const char *body,
                           size_t body_len,
                           int *out_status,
                           Buffer *out_json) {
  (void)path; (void)qs;
  if(!ctx->tokenizer) {
    *out_status = 404;
    buffer_text(out_json, "{\"error\":\"not_found\"}");
    return 0;
  }
  char *text = malloc(16384u);
  if(!text) {
    *out_status = 500;
    buffer_text(out_json, "{\"error\":\"out_of_memory\"}");
    return 0;
  }
  json_extract_field(body, body_len, "text", text, 16384u);
  uint32_t *tokens = NULL;
  size_t count = 0;
  m1_tokenizer_encode(ctx->tokenizer, (const uint8_t *)text, strlen(text), &tokens, &count);
  free(text);
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

typedef int (*HttpRouteHandler)(ServerContext *ctx,
                                const char *path,
                                const char *qs,
                                const char *body,
                                size_t body_len,
                                int *out_status,
                                Buffer *out_json);

typedef struct {
  const char *method;
  const char *path;
  int prefix_match;
  int is_write;
  const char *content_type;
  HttpRouteHandler handler;
} HttpRoute;

static const HttpRoute k_http_routes[] = {
  {"GET", "/console", 0, 0, "text/html; charset=utf-8", handle_console},
  {"GET", "/index.html", 0, 0, "text/html; charset=utf-8", handle_console},
  {"GET", "/", 0, 0, NULL, handle_health},
  {"GET", "/health", 0, 0, NULL, handle_health},
  {"GET", "/api/v1/stats", 0, 0, NULL, handle_stats},
  {"GET", "/api/v1/ontology", 0, 0, NULL, handle_ontology},
  {"GET", "/api/v1/facets", 0, 0, NULL, handle_facets},
  {"GET", "/api/v1/trending", 0, 0, NULL, handle_facets},
  {"GET", "/api/v1/concepts", 0, 0, NULL, handle_concepts},
  {"GET", "/api/v1/pullback", 0, 0, NULL, handle_pullback},
  {"GET", "/api/v1/pushout", 0, 0, NULL, handle_pushout},
  {"GET", "/api/v1/fibration", 0, 0, NULL, handle_fibration},
  {"GET", "/api/v1/temporal", 0, 0, NULL, handle_temporal},
  {"GET", "/api/v1/topos", 0, 0, NULL, handle_topos},
  {"GET", "/api/v1/operad", 0, 0, NULL, handle_operad},
  {"GET", "/api/v1/feed.xml", 0, 0, "application/atom+xml; charset=utf-8", handle_feed_xml},
  {"GET", "/api/v1/stream", 0, 0, "text/event-stream; charset=utf-8", handle_stream},
  {"GET", "/api/v1/centrality", 0, 0, NULL, handle_centrality},
  {"GET", "/api/v1/catalog", 0, 0, NULL, handle_catalog},
  {"POST", "/api/v1/catalog/seed", 0, 1, NULL, handle_catalog_seed},
  {"GET", "/api/v1/feeds", 0, 0, NULL, handle_feeds},
  {"POST", "/api/v1/feeds/poll", 0, 1, NULL, handle_feeds_poll},
  {"GET", "/api/v1/markets", 0, 0, NULL, handle_markets},
  {"GET", "/api/v1/security", 0, 0, NULL, handle_security},
  {"GET", "/api/v1/ai", 0, 0, NULL, handle_ai},
  {"GET", "/api/v1/sports", 0, 0, NULL, handle_sports},
  {"GET", "/api/v1/weather", 0, 0, NULL, handle_weather},
  {"GET", "/api/v1/automation", 0, 0, NULL, handle_automation_get},
  {"POST", "/api/v1/automation/rules", 0, 1, NULL, handle_automation_rules_post},
  {"DELETE", "/api/v1/automation/rules", 0, 1, NULL, handle_automation_rules_delete},
  {"POST", "/api/v1/automation/run", 0, 1, NULL, handle_automation_run},
  {"GET", "/api/v1/appliance", 0, 0, NULL, handle_appliance},
  {"GET", "/api/v1/machine", 0, 0, NULL, handle_appliance},
  {"GET", "/api/v1/articles", 0, 0, NULL, handle_articles_search},
  {"GET", "/api/v1/articles/", 1, 0, NULL, handle_article_by_id},
  {"POST", "/api/v1/articles/", 1, 1, NULL, handle_article_enrich},
  {"POST", "/api/v1/extract", 0, 1, NULL, handle_extract},
  {"POST", "/api/v1/tokenize", 0, 0, NULL, handle_tokenize}
};

static const HttpRoute *find_http_route(const char *method,
                                        const char *target,
                                        char *out_path,
                                        size_t out_path_cap,
                                        const char **out_qs) {
  if(!method || !target || !out_path || out_path_cap == 0) return NULL;
  const char *qmark = strchr(target, '?');
  size_t path_len = qmark ? (size_t)(qmark - target) : strlen(target);
  if(path_len >= out_path_cap) path_len = out_path_cap - 1u;
  memcpy(out_path, target, path_len);
  out_path[path_len] = '\0';
  while(path_len > 1u && out_path[path_len - 1u] == '/') {
    out_path[--path_len] = '\0';
  }
  if(out_qs) *out_qs = qmark ? (qmark + 1u) : "";

  size_t nroutes = sizeof(k_http_routes) / sizeof(k_http_routes[0]);
  for(size_t i = 0; i < nroutes; ++i) {
    const HttpRoute *r = &k_http_routes[i];
    if(strcmp(method, r->method) != 0) continue;
    if(r->prefix_match) {
      if(strncmp(out_path, r->path, strlen(r->path)) == 0) return r;
    } else {
      if(strcmp(out_path, r->path) == 0) return r;
    }
  }
  return NULL;
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
  const char *qs = "";
  const HttpRoute *route = find_http_route(method, target, path, sizeof(path), &qs);
  if(route && route->handler) {
    return route->handler(ctx, path, qs, body, body_len, out_status, out_json);
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
    case 413: return "Content Too Large";
    case 404: return "Not Found";
    default: return "Internal Server Error";
  }
}

typedef struct {
  ServerContext *ctx;
  int client_fd;
} ClientConnTask;

static int send_all(int fd, const char *data, size_t len) {
  size_t sent = 0;
  while(sent < len) {
    ssize_t w = send(fd, data + sent, len - sent, 0);
    if(w <= 0) return -1;
    sent += (size_t)w;
  }
  return 0;
}

static size_t request_header_end(const char *req, size_t used) {
  for(size_t i = 0; i + 3u < used; ++i) {
    if(req[i] == '\r' && req[i + 1u] == '\n' && req[i + 2u] == '\r' && req[i + 3u] == '\n') {
      return i + 4u;
    }
  }
  return 0;
}

static int header_has_token(const char *value, size_t len, const char *token) {
  size_t token_len = strlen(token);
  for(size_t i = 0; i < len;) {
    while(i < len && (value[i] == ' ' || value[i] == '\t' || value[i] == ',')) i++;
    size_t start = i;
    while(i < len && value[i] != ',') i++;
    size_t end = i;
    while(end > start && (value[end - 1u] == ' ' || value[end - 1u] == '\t')) end--;
    if(end - start == token_len && strncasecmp(value + start, token, token_len) == 0) return 1;
  }
  return 0;
}

static int parse_request_headers(char *req, size_t hdr_len, size_t cap,
                                 char method[16], char target[1024],
                                 size_t *body_len, int *want_close) {
  for(size_t i = 0; i < hdr_len; ++i) {
    unsigned char c = (unsigned char)req[i];
    if(c == 0 || (c < 0x20u && c != '\r' && c != '\n' && c != '\t') || c == 0x7fu) return 400;
    if(c == '\r' && (i + 1u >= hdr_len || req[i + 1u] != '\n')) return 400;
    if(c == '\n' && (i == 0 || req[i - 1u] != '\r')) return 400;
  }
  char *first_end = strstr(req, "\r\n");
  if(!first_end || (size_t)(first_end - req) >= hdr_len) return 400;
  char saved = *first_end;
  *first_end = '\0';
  char version[16] = {0};
  char extra = 0;
  int parts = sscanf(req, "%15s %1023s %15s %c", method, target, version, &extra);
  *first_end = saved;
  if(parts != 3 || (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) ||
     target[0] != '/') return 400;

  *body_len = 0;
  *want_close = strcmp(version, "HTTP/1.0") == 0;
  int saw_length = 0;
  int saw_close = 0, saw_keep_alive = 0;
  char *p = first_end + 2u;
  char *headers_end = req + hdr_len - 4u;
  while(p < headers_end) {
    char *end = strstr(p, "\r\n");
    if(!end || end > headers_end || end == p) return 400;
    char *colon = memchr(p, ':', (size_t)(end - p));
    if(!colon || colon == p) return 400;
    for(char *q = p; q < colon; ++q) {
      unsigned char c = (unsigned char)*q;
      int ascii_alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z');
      if(!ascii_alnum && !strchr("!#$%&'*+-.^_`|~", c)) return 400;
    }
    const char *value = colon + 1u;
    while(value < end && (*value == ' ' || *value == '\t')) value++;
    size_t name_len = (size_t)(colon - p);
    if(name_len == 14u && strncasecmp(p, "Content-Length", 14u) == 0) {
      if(saw_length || value == end) return 400;
      saw_length = 1;
      size_t length = 0;
      const char *q = value;
      while(q < end && *q >= '0' && *q <= '9') {
        unsigned digit = (unsigned)(*q++ - '0');
        if(length > (SIZE_MAX - digit) / 10u) return 413;
        length = length * 10u + digit;
      }
      if(q == value) return 400;
      while(q < end && (*q == ' ' || *q == '\t')) q++;
      if(q != end) return 400;
      *body_len = length;
    } else if(name_len == 17u && strncasecmp(p, "Transfer-Encoding", 17u) == 0) {
      return 400; /* Chunked bodies are not implemented. */
    } else if(name_len == 10u && strncasecmp(p, "Connection", 10u) == 0) {
      saw_close |= header_has_token(value, (size_t)(end - value), "close");
      saw_keep_alive |= header_has_token(value, (size_t)(end - value), "keep-alive");
    }
    p = end + 2u;
  }
  *want_close = saw_close || (strcmp(version, "HTTP/1.0") == 0 && !saw_keep_alive);
  if(*body_len > cap - hdr_len - 1u) return 413;
  return 0;
}

static void send_request_error(int client, int status) {
  char response[192];
  int n = snprintf(response, sizeof(response),
                   "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                   status, status_reason(status));
  if(n > 0 && (size_t)n < sizeof(response)) send_all(client, response, (size_t)n);
}

void http_serve_client(ServerContext *ctx, int client) {
  if(!ctx || client < 0) return;

#ifdef SO_NOSIGPIPE
  int nosig = 1;
  setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
  struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  const size_t req_cap = 65536u;
  char *req = malloc(req_cap);
  if(!req) {
    close(client);
    return;
  }

  size_t used = 0;
  /* Serve up to 16 pipelined / keep-alive HTTP/1.1 requests per connection. */
  for(int req_idx = 0; req_idx < 16; ++req_idx) {
    char method[16] = {0};
    char target[1024] = {0};
    size_t hdr_len = 0, expected_body = 0;
    int want_close = 0;
    int parse_status = 0;
    for(;;) {
      hdr_len = request_header_end(req, used);
      if(hdr_len) {
        parse_status = parse_request_headers(req, hdr_len, req_cap, method, target,
                                             &expected_body, &want_close);
        if(parse_status) break;
        if(used >= hdr_len + expected_body) break;
      }
      if(used + 1u >= req_cap) {
        parse_status = 413;
        break;
      }
      ssize_t n = recv(client, req + used, req_cap - 1u - used, 0);
      if(n <= 0) break;
      used += (size_t)n;
      req[used] = '\0';
    }
    if(parse_status) {
      send_request_error(client, parse_status);
      break;
    }
    if(!hdr_len || used < hdr_len + expected_body) break;

    if(strcmp(method, "OPTIONS") == 0) {
      char cors[256];
      int cors_len = snprintf(cors, sizeof(cors),
          "HTTP/1.1 204 No Content\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
          "Access-Control-Allow-Headers: Content-Type\r\n"
          "Connection: %s\r\n\r\n", want_close ? "close" : "keep-alive");
      if(cors_len < 0 || (size_t)cors_len >= sizeof(cors) ||
         send_all(client, cors, (size_t)cors_len) != 0 || want_close) break;
      size_t consumed = hdr_len + expected_body;
      used -= consumed;
      memmove(req, req + consumed, used);
      req[used] = '\0';
      continue;
    }

    const char *body = req + hdr_len;
    size_t body_len = expected_body;

    char route_path[512];
    const HttpRoute *matched = find_http_route(method, target, route_path, sizeof(route_path), NULL);
    int is_write = (matched && matched->is_write) ? 1 : 0;
    if(is_write) engine_write_lock(ctx->engine);
    else engine_read_lock(ctx->engine);

    int status = 200;
    Buffer payload;
    http_handle_request(ctx, method, target, body, body_len, &status, &payload);

    engine_unlock(ctx->engine);

    const char *ctype = (matched && matched->content_type)
                            ? matched->content_type
                            : "application/json; charset=utf-8";
    Buffer resp;
    buffer_init(&resp);
    buffer_format(&resp,
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %zu\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: %s\r\n\r\n",
                  status,
                  status_reason(status),
                  ctype,
                  payload.len,
                  want_close ? "close" : "keep-alive");
    if(payload.len > 0) buffer_append(&resp, payload.data, payload.len);

    int send_err = 0;
    if(resp.data && resp.len > 0) {
      if(send_all(client, resp.data, resp.len) != 0) send_err = 1;
    }
    buffer_free(&payload);
    buffer_free(&resp);
    if(want_close || send_err) break;
    size_t consumed = hdr_len + expected_body;
    used -= consumed;
    memmove(req, req + consumed, used);
    req[used] = '\0';
  }

  free(req);
  close(client);
}

static void *http_client_worker(void *arg) {
  ClientConnTask *task = (ClientConnTask *)arg;
  ServerContext *ctx = task->ctx;
  int client = task->client_fd;
  free(task);
  http_serve_client(ctx, client);
  return NULL;
}

static void *http_auto_daemon_worker(void *arg) {
  ServerContext *ctx = (ServerContext *)arg;
  for(;;) {
    engine_read_lock(ctx->engine);
    uint32_t interval_sec = ctx->engine->auto_poll_interval_sec;
    engine_unlock(ctx->engine);
    if(interval_sec == 0u) {
      sleep(5u);
      continue;
    }
    sleep(interval_sec);
    engine_write_lock(ctx->engine);
    if(ctx->engine->auto_poll_interval_sec > 0u) {
      engine_poll_bandit_auto(ctx->engine, 8u, 4u);
      if(ctx->snapshot_path) engine_save_snapshot(ctx->engine, ctx->snapshot_path);
    }
    engine_unlock(ctx->engine);
  }
  return NULL;
}

int http_server_listen(ServerContext *ctx) {
  if(!ctx) return -1;
  signal(SIGPIPE, SIG_IGN);
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
  if(listen(srv, 128) < 0) {
    close(srv);
    return -1;
  }

  pthread_t auto_tid;
  if(pthread_create(&auto_tid, NULL, http_auto_daemon_worker, ctx) == 0) {
    pthread_detach(auto_tid);
  }

  printf("oyez c99 server listening on http://127.0.0.1:%u (feeds=%zu, articles=%zu, rules=%zu)\n",
         ctx->port ? ctx->port : 8787u,
         ctx->engine->feed_count,
         ctx->engine->article_count,
         ctx->engine->automation_rule_count);
  fflush(stdout);

  for(;;) {
    int client = accept(srv, NULL, NULL);
    if(client < 0) continue;

    ClientConnTask *task = malloc(sizeof(ClientConnTask));
    if(!task) {
      close(client);
      continue;
    }
    task->ctx = ctx;
    task->client_fd = client;

    pthread_t tid;
    if(pthread_create(&tid, NULL, http_client_worker, task) == 0) {
      pthread_detach(tid);
    } else {
      http_client_worker(task);
    }
  }
}
