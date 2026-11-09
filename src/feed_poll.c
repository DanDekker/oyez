#include "http_server.h"

#include "markup.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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

/* Fetch and parse before taking the lock used to update the index. */
static const char *fetch_feed(const char *feed_url, const char *site_url, ParsedFeed *parsed) {
  Buffer raw;
  if(http_fetch_url(feed_url, &raw) != 0) return "fetch_failed";
  int rc = feed_parse(raw.data, raw.len, site_url, parsed);
  buffer_free(&raw);
  return rc == 0 ? NULL : "parse_failed";
}

int engine_refresh_feed(OyezEngine *engine, uint32_t feed_id, size_t *out_inserted) {
  if(out_inserted) *out_inserted = 0;
  if(!engine) return -1;
  FeedRecord *feed = (FeedRecord *)engine_find_feed(engine, feed_id);
  if(!feed) return -1;

  ParsedFeed parsed;
  const char *error = fetch_feed(feed->feed_url, feed->site_url, &parsed);
  if(error) {
    feed->error_count++;
    set_feed_error(feed, error);
    return -1;
  }

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

enum { MAX_POLL_THREADS = 16 };

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

    ParsedFeed parsed;
    const char *error = fetch_feed(feed_url, site_url, &parsed);
    if(error) {
      pthread_mutex_lock(&pool->eng_lock);
      pool->engine->feeds[idx].error_count++;
      set_feed_error(&pool->engine->feeds[idx], error);
      pool->engine->total_bandit_pulls++;
      pthread_mutex_unlock(&pool->eng_lock);
      continue;
    }

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
  if(thread_count > MAX_POLL_THREADS) thread_count = MAX_POLL_THREADS;
  if(thread_count > limit) thread_count = limit;

  PollPool pool = {.engine = engine, .feed_ids = feed_ids, .limit = limit};
  if(pthread_mutex_init(&pool.idx_lock, NULL) != 0) return 0;
  if(pthread_mutex_init(&pool.eng_lock, NULL) != 0) {
    pthread_mutex_destroy(&pool.idx_lock);
    return 0;
  }

  pthread_t threads[MAX_POLL_THREADS - 1];
  size_t started = 0;
  while(started + 1u < thread_count) {
    if(pthread_create(&threads[started], NULL, poll_worker_main, &pool) != 0) break;
    ++started;
  }
  poll_worker_main(&pool);
  for(size_t t = 0; t < started; ++t) {
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
