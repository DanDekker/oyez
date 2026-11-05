#ifndef OYEZ_HTTP_SERVER_H
#define OYEZ_HTTP_SERVER_H

#include "index_engine.h"
#include "m1_tokenizer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  OyezEngine *engine;
  M1Tokenizer *tokenizer;
  const char *snapshot_path;
  uint16_t port;
} ServerContext;

int http_fetch_url(const char *url, Buffer *out);
/* Resolves the host and rejects local, private, and non-global destinations. */
int http_url_is_public(const char *url);
int http_resolve_redirect_url(const char *base_url,
                              const char *location,
                              char *out,
                              size_t out_cap);
int http_client_exchange_fd(int fd,
                            const char *host,
                            uint16_t port,
                            int default_port,
                            const char *request_path,
                            int *out_status,
                            char *out_location,
                            size_t location_cap,
                            Buffer *out_body);
int engine_refresh_feed(OyezEngine *engine, uint32_t feed_id, size_t *out_inserted);
size_t engine_poll_all(OyezEngine *engine, size_t max_feeds);
size_t engine_poll_concurrent(OyezEngine *engine, size_t max_feeds, size_t thread_count);
size_t engine_poll_bandit_auto(OyezEngine *engine, size_t batch_size, size_t thread_count);

int http_handle_request(ServerContext *ctx,
                        const char *method,
                        const char *target,
                        const char *body,
                        size_t body_len,
                        int *out_status,
                        Buffer *out_json);

int http_server_listen(ServerContext *ctx);
/* Takes ownership of client_fd and closes it before returning. */
void http_serve_client(ServerContext *ctx, int client_fd);

#ifdef __cplusplus
}
#endif

#endif
