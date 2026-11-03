#ifndef OYEZ_HTTP_SERVER_H
#define OYEZ_HTTP_SERVER_H

#include "index_engine.h"
#include "m1_tokenizer.h"

#include <stdint.h>

typedef struct {
  OyezEngine *engine;
  M1Tokenizer *tokenizer;
  const char *snapshot_path;
  uint16_t port;
} ServerContext;

int http_fetch_url(const char *url, Buffer *out);
int engine_refresh_feed(OyezEngine *engine, uint32_t feed_id, size_t *out_inserted);
size_t engine_poll_all(OyezEngine *engine, size_t max_feeds);

int http_handle_request(ServerContext *ctx,
                        const char *method,
                        const char *target,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json);

int http_server_listen(ServerContext *ctx);

#endif
