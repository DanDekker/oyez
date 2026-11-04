#include "http_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define FETCH_URL_CAP 2048u
#define FETCH_BODY_CAP (8u * 1024u * 1024u)
#define FETCH_HEADER_CAP 65536u

typedef struct {
  char host[256];
  char port[6];
  char path[FETCH_URL_CAP];
  unsigned port_num;
  int is_https;
  int is_ipv6;
  int literal;
} FetchTarget;

static int parse_fetch_url(const char *url, FetchTarget *target) {
  if(!url || !target) return -1;
  size_t len = strlen(url);
  if(len == 0 || len >= FETCH_URL_CAP) return -1;
  const char *authority = NULL;
  unsigned port = 0;
  int is_https = 0;
  if(strncmp(url, "http://", 7u) == 0) {
    authority = url + 7u;
    port = 80u;
    is_https = 0;
  } else if(strncmp(url, "https://", 8u) == 0) {
    authority = url + 8u;
    port = 443u;
    is_https = 1;
  } else {
    return -1;
  }
  for(const unsigned char *p = (const unsigned char *)url; *p; ++p) {
    if(*p <= 0x20u || *p >= 0x7fu || *p == '\\' || *p == '#') return -1;
  }

  const char *end = authority;
  while(*end && *end != '/' && *end != '?') end++;
  if(end == authority || memchr(authority, '@', (size_t)(end - authority)) ||
     memchr(authority, '%', (size_t)(end - authority))) return -1;

  const char *port_start = NULL;
  memset(target, 0, sizeof(*target));
  target->is_https = is_https;
  if(*authority == '[') {
    const char *close = memchr(authority, ']', (size_t)(end - authority));
    if(!close || close == authority + 1u || (size_t)(close - authority - 1u) >= sizeof(target->host)) return -1;
    size_t host_len = (size_t)(close - authority - 1u);
    memcpy(target->host, authority + 1u, host_len);
    target->host[host_len] = '\0';
    struct in6_addr addr6;
    if(inet_pton(AF_INET6, target->host, &addr6) != 1) return -1;
    target->literal = 1;
    target->is_ipv6 = 1;
    if(close + 1u != end) {
      if(close[1] != ':') return -1;
      port_start = close + 2u;
    }
  } else {
    const char *colon = memchr(authority, ':', (size_t)(end - authority));
    const char *host_end = colon ? colon : end;
    size_t host_len = (size_t)(host_end - authority);
    if(host_len == 0 || host_len >= sizeof(target->host)) return -1;
    memcpy(target->host, authority, host_len);
    target->host[host_len] = '\0';
    if(colon) port_start = colon + 1u;

    struct in_addr addr4;
    if(inet_pton(AF_INET, target->host, &addr4) == 1) {
      target->literal = 1;
    } else {
      int only_numeric = 1;
      int label_start = 1;
      for(size_t i = 0; i < host_len; ++i) {
        unsigned char c = (unsigned char)target->host[i];
        if(isalpha(c)) only_numeric = 0;
        if(c == '.') {
          if(label_start || target->host[i - 1u] == '-') return -1;
          label_start = 1;
        } else if(isalnum(c) || c == '-') {
          if(label_start && c == '-') return -1;
          label_start = 0;
        } else {
          return -1;
        }
      }
      if(label_start || target->host[host_len - 1u] == '-' || only_numeric) return -1;
    }
  }

  if(port_start) {
    if(port_start == end) return -1;
    port = 0u;
    for(const char *p = port_start; p < end; ++p) {
      if(*p < '0' || *p > '9') return -1;
      unsigned digit = (unsigned)(*p - '0');
      if(port > (65535u - digit) / 10u) return -1;
      port = port * 10u + digit;
    }
    if(port == 0u) return -1;
  }
  target->port_num = port;
  snprintf(target->port, sizeof(target->port), "%u", port);
  if(*end == '\0') {
    strcpy(target->path, "/");
  } else if(*end == '?') {
    int pn = snprintf(target->path, sizeof(target->path), "/%s", end);
    if(pn < 0 || (size_t)pn >= sizeof(target->path)) return -1;
  } else {
    size_t plen = strlen(end);
    if(plen >= sizeof(target->path)) return -1;
    memcpy(target->path, end, plen + 1u);
  }
  return 0;
}

static int public_ipv4(const struct in_addr *addr) {
  const unsigned char *b = (const unsigned char *)&addr->s_addr;
  unsigned a = b[0], c = b[1], d = b[2];
  if(a == 0u || a == 10u || a == 127u || a >= 224u) return 0;
  if(a == 100u && c >= 64u && c <= 127u) return 0;
  if(a == 169u && c == 254u) return 0;
  if(a == 172u && c >= 16u && c <= 31u) return 0;
  if(a == 192u && (c == 168u || (c == 0u && d == 0u) || (c == 0u && d == 2u))) return 0;
  if(a == 198u && ((c == 18u || c == 19u) || (c == 51u && d == 100u))) return 0;
  if(a == 203u && c == 0u && d == 113u) return 0;
  return 1;
}

static int public_ipv6(const struct in6_addr *addr) {
  const unsigned char *b = addr->s6_addr;
  if((b[0] & 0xe0u) != 0x20u) return 0; /* Global unicast only. */
  if(b[0] == 0x20u && b[1] == 0x02u) return 0; /* 6to4 embeds an IPv4 destination. */
  if(b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x0du && b[3] == 0xb8u) return 0;
  if(b[0] == 0x20u && b[1] == 0x01u && b[2] == 0u && b[3] == 0u) return 0;
  return 1;
}

static int resolve_public_endpoint(const FetchTarget *target,
                                   char ip[INET6_ADDRSTRLEN],
                                   struct sockaddr_storage *addr_out,
                                   socklen_t *addr_len_out) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV;
  struct addrinfo *results = NULL;
  if(getaddrinfo(target->host, target->port, &hints, &results) != 0) return -1;

  int found = 0;
  int chosen_family = 0;
  for(const struct addrinfo *ai = results; ai; ai = ai->ai_next) {
    const void *address = NULL;
    if(ai->ai_family == AF_INET) {
      const struct in_addr *a = &((const struct sockaddr_in *)ai->ai_addr)->sin_addr;
      if(!public_ipv4(a)) { found = -1; break; }
      address = a;
    } else if(ai->ai_family == AF_INET6) {
      const struct in6_addr *a = &((const struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
      if(!public_ipv6(a)) { found = -1; break; }
      address = a;
    } else {
      continue;
    }
    if(!chosen_family || (chosen_family == AF_INET6 && ai->ai_family == AF_INET)) {
      if(!inet_ntop(ai->ai_family, address, ip, INET6_ADDRSTRLEN)) { found = -1; break; }
      if(addr_out && addr_len_out && ai->ai_addr && ai->ai_addrlen <= sizeof(*addr_out)) {
        memset(addr_out, 0, sizeof(*addr_out));
        memcpy(addr_out, ai->ai_addr, (size_t)ai->ai_addrlen);
        *addr_len_out = ai->ai_addrlen;
      }
      chosen_family = ai->ai_family;
      found = 1;
    }
  }
  freeaddrinfo(results);
  return found == 1 ? 0 : -1;
}

static int resolve_public_ip(const FetchTarget *target, char ip[INET6_ADDRSTRLEN]) {
  return resolve_public_endpoint(target, ip, NULL, NULL);
}

int http_url_is_public(const char *url) {
  FetchTarget target;
  char ip[INET6_ADDRSTRLEN];
  return parse_fetch_url(url, &target) == 0 && resolve_public_ip(&target, ip) == 0;
}

static int normalize_uri_path(const char *path_and_query, char *out, size_t out_cap) {
  if(!path_and_query || !out || out_cap < 2u) return -1;
  const char *q = strchr(path_and_query, '?');
  size_t path_len = q ? (size_t)(q - path_and_query) : strlen(path_and_query);
  const char *query = q ? q : "";
  size_t query_len = strlen(query);

  size_t seg_start[64];
  size_t seg_len[64];
  size_t depth = 0;
  int trailing_slash = (path_len > 1u && path_and_query[path_len - 1u] == '/');

  size_t i = 0;
  while(i < path_len) {
    while(i < path_len && path_and_query[i] == '/') i++;
    if(i >= path_len) break;
    size_t s = i;
    while(i < path_len && path_and_query[i] != '/') i++;
    size_t slen = i - s;
    if(slen == 1u && path_and_query[s] == '.') {
      if(i == path_len) trailing_slash = 1;
      continue;
    }
    if(slen == 2u && path_and_query[s] == '.' && path_and_query[s + 1u] == '.') {
      if(depth > 0) depth--;
      if(i == path_len) trailing_slash = 1;
      continue;
    }
    if(depth >= 64u) return -1;
    seg_start[depth] = s;
    seg_len[depth] = slen;
    depth++;
  }

  size_t pos = 0;
  if(depth == 0) {
    out[pos++] = '/';
  } else {
    for(size_t d = 0; d < depth; ++d) {
      if(pos + 1u + seg_len[d] >= out_cap) return -1;
      out[pos++] = '/';
      memcpy(out + pos, path_and_query + seg_start[d], seg_len[d]);
      pos += seg_len[d];
    }
    if(trailing_slash) {
      if(pos + 1u >= out_cap) return -1;
      out[pos++] = '/';
    }
  }
  if(pos + query_len >= out_cap) return -1;
  if(query_len > 0) {
    memcpy(out + pos, query, query_len);
    pos += query_len;
  }
  out[pos] = '\0';
  return 0;
}

int http_resolve_redirect_url(const char *base_url,
                              const char *location,
                              char *out,
                              size_t out_cap) {
  if(!base_url || !location || !out || out_cap == 0) return -1;
  out[0] = '\0';
  while(*location && (*location == ' ' || *location == '\t')) location++;
  size_t loc_len = strlen(location);
  while(loc_len > 0 &&
        (location[loc_len - 1u] == ' ' || location[loc_len - 1u] == '\t' ||
         location[loc_len - 1u] == '\r' || location[loc_len - 1u] == '\n')) {
    loc_len--;
  }
  const char *hash = memchr(location, '#', loc_len);
  if(hash) loc_len = (size_t)(hash - location);
  if(loc_len == 0 || loc_len >= FETCH_URL_CAP) return -1;

  char clean_loc[FETCH_URL_CAP];
  memcpy(clean_loc, location, loc_len);
  clean_loc[loc_len] = '\0';
  for(size_t i = 0; i < loc_len; ++i) {
    unsigned char c = (unsigned char)clean_loc[i];
    if(c <= 0x20u || c >= 0x7fu || c == '\\') return -1;
  }

  if(strncmp(clean_loc, "http://", 7u) == 0 || strncmp(clean_loc, "https://", 8u) == 0) {
    if(loc_len >= out_cap) return -1;
    memcpy(out, clean_loc, loc_len + 1u);
    return 0;
  }

  FetchTarget base;
  if(parse_fetch_url(base_url, &base) != 0) return -1;
  const char *scheme = base.is_https ? "https" : "http";
  unsigned default_port = base.is_https ? 443u : 80u;

  if(clean_loc[0] == '/' && clean_loc[1] == '/') {
    int n = snprintf(out, out_cap, "%s:%s", scheme, clean_loc);
    return (n > 0 && (size_t)n < out_cap) ? 0 : -1;
  }

  char origin[512];
  int on = 0;
  if(base.port_num == default_port) {
    on = snprintf(origin, sizeof(origin), "%s://%s%s%s",
                  scheme, base.is_ipv6 ? "[" : "", base.host, base.is_ipv6 ? "]" : "");
  } else {
    on = snprintf(origin, sizeof(origin), "%s://%s%s%s:%u",
                  scheme, base.is_ipv6 ? "[" : "", base.host, base.is_ipv6 ? "]" : "", base.port_num);
  }
  if(on <= 0 || (size_t)on >= sizeof(origin)) return -1;

  char raw_path[FETCH_URL_CAP];
  if(clean_loc[0] == '/') {
    int pn = snprintf(raw_path, sizeof(raw_path), "%s", clean_loc);
    if(pn <= 0 || (size_t)pn >= sizeof(raw_path)) return -1;
  } else {
    const char *q = strchr(base.path, '?');
    size_t path_end = q ? (size_t)(q - base.path) : strlen(base.path);
    size_t last_slash = 0;
    for(size_t i = 0; i < path_end; ++i) {
      if(base.path[i] == '/') last_slash = i;
    }
    int pn = snprintf(raw_path, sizeof(raw_path), "%.*s%s", (int)(last_slash + 1u), base.path, clean_loc);
    if(pn <= 0 || (size_t)pn >= sizeof(raw_path)) return -1;
  }

  char norm_path[FETCH_URL_CAP];
  if(normalize_uri_path(raw_path, norm_path, sizeof(norm_path)) != 0) return -1;

  int n = snprintf(out, out_cap, "%s%s", origin, norm_path);
  return (n > 0 && (size_t)n < out_cap) ? 0 : -1;
}

static int ci_header_prefix(const char *line, size_t len, const char *name) {
  size_t nlen = strlen(name);
  if(len < nlen) return 0;
  for(size_t i = 0; i < nlen; ++i) {
    if(tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) return 0;
  }
  return 1;
}

static int ci_contains_token(const char *val, size_t len, const char *token) {
  size_t tlen = strlen(token);
  if(tlen == 0 || len < tlen) return 0;
  for(size_t i = 0; i + tlen <= len; ++i) {
    if(ci_header_prefix(val + i, len - i, token)) return 1;
  }
  return 0;
}

static int write_all_timeout(int fd, const char *buf, size_t len, int timeout_ms) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  size_t sent = 0;
  while(sent < len) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int pr = poll(&pfd, 1u, timeout_ms);
    if(pr < 0 && errno == EINTR) continue;
    if(pr <= 0) return -1;
#ifdef MSG_NOSIGNAL
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
#else
    ssize_t n = write(fd, buf + sent, len - sent);
#endif
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n <= 0) return -1;
    sent += (size_t)n;
  }
  return 0;
}

/* Returns 0 when complete (including terminal 0-chunk and trailer CRLF),
 * 1 when prefix is valid but more bytes are required, and -1 on error. */
static int decode_chunked_body(const char *raw, size_t raw_len, Buffer *out_body) {
  size_t pos = 0;
  while(pos < raw_len) {
    size_t line_end = pos;
    while(line_end + 1u < raw_len && !(raw[line_end] == '\r' && raw[line_end + 1u] == '\n')) {
      line_end++;
    }
    if(line_end + 1u >= raw_len) return 1;
    size_t p = pos;
    while(p < line_end && (raw[p] == ' ' || raw[p] == '\t')) p++;
    size_t chunk_sz = 0;
    int digits = 0;
    while(p < line_end) {
      unsigned char c = (unsigned char)raw[p];
      unsigned v = 0;
      if(c >= '0' && c <= '9') v = (unsigned)(c - '0');
      else if(c >= 'a' && c <= 'f') v = 10u + (unsigned)(c - 'a');
      else if(c >= 'A' && c <= 'F') v = 10u + (unsigned)(c - 'A');
      else break;
      if(chunk_sz > (FETCH_BODY_CAP - v) / 16u) return -1;
      chunk_sz = chunk_sz * 16u + v;
      digits++;
      p++;
    }
    while(p < line_end && (raw[p] == ' ' || raw[p] == '\t')) p++;
    if(digits == 0 || (p < line_end && raw[p] != ';')) return -1;
    pos = line_end + 2u;
    if(chunk_sz == 0u) {
      while(pos < raw_len) {
        size_t tr_end = pos;
        while(tr_end + 1u < raw_len && !(raw[tr_end] == '\r' && raw[tr_end + 1u] == '\n')) {
          tr_end++;
        }
        if(tr_end + 1u >= raw_len) return 1;
        if(tr_end == pos) {
          return 0;
        }
        pos = tr_end + 2u;
      }
      return 1;
    }
    if(chunk_sz > FETCH_BODY_CAP - out_body->len) return -1;
    if(pos + chunk_sz + 2u > raw_len) return 1;
    if(raw[pos + chunk_sz] != '\r' || raw[pos + chunk_sz + 1u] != '\n') return -1;
    buffer_append(out_body, raw + pos, chunk_sz);
    if(out_body->truncated) return -1;
    pos += chunk_sz + 2u;
  }
  return 1;
}

int http_client_exchange_fd(int fd,
                            const char *host,
                            uint16_t port,
                            int default_port,
                            const char *request_path,
                            int *out_status,
                            char *out_location,
                            size_t location_cap,
                            Buffer *out_body) {
  if(fd < 0 || !host || !host[0] || !out_status || !out_body) return -1;
  *out_status = 0;
  if(out_location && location_cap > 0) out_location[0] = '\0';
  buffer_clear(out_body);

  const char *path = (request_path && request_path[0] == '/') ? request_path : "/";
  int is_v6 = strchr(host, ':') != NULL;
  char host_hdr[320];
  int hn = 0;
  if((int)port == default_port || port == 0u) {
    hn = snprintf(host_hdr, sizeof(host_hdr), "%s%s%s", is_v6 ? "[" : "", host, is_v6 ? "]" : "");
  } else {
    hn = snprintf(host_hdr, sizeof(host_hdr), "%s%s%s:%u",
                  is_v6 ? "[" : "", host, is_v6 ? "]" : "", (unsigned)port);
  }
  if(hn <= 0 || (size_t)hn >= sizeof(host_hdr)) return -1;

  Buffer req;
  buffer_init(&req);
  buffer_format(&req,
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: Oyez/2.0 (Pure First-Party C99 Intelligence Engine)\r\n"
                "Accept: application/rss+xml, application/atom+xml, application/json, text/xml, text/html;q=0.9, */*;q=0.8\r\n"
                "Accept-Encoding: identity\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, host_hdr);
  if(req.truncated || !req.data) {
    buffer_free(&req);
    return -1;
  }
  int wrc = write_all_timeout(fd, req.data, req.len, 5000);
  buffer_free(&req);
  if(wrc != 0) return -1;

  Buffer raw;
  buffer_init(&raw);
  size_t header_end = 0;
  int found_headers = 0;

  while(!found_headers) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr = poll(&pfd, 1u, 8000);
    if(pr < 0 && errno == EINTR) continue;
    if(pr <= 0) {
      buffer_free(&raw);
      return -1;
    }
    char chunk[8192];
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n <= 0) {
      buffer_free(&raw);
      return -1;
    }
    buffer_append(&raw, chunk, (size_t)n);
    if(raw.truncated) {
      buffer_free(&raw);
      return -1;
    }
    for(size_t i = 0; i + 3u < raw.len; ++i) {
      if(raw.data[i] == '\r' && raw.data[i + 1u] == '\n' &&
         raw.data[i + 2u] == '\r' && raw.data[i + 3u] == '\n') {
        header_end = i + 4u;
        found_headers = 1;
        break;
      }
    }
    if(!found_headers && raw.len > FETCH_HEADER_CAP) {
      buffer_free(&raw);
      return -1;
    }
  }

  /* Parse HTTP/1.x status line and headers */
  const char *hdr = raw.data;
  size_t first_crlf = 0;
  while(first_crlf + 1u < header_end && !(hdr[first_crlf] == '\r' && hdr[first_crlf + 1u] == '\n')) {
    first_crlf++;
  }
  if(first_crlf < 12u || strncmp(hdr, "HTTP/1.", 7u) != 0 ||
     (hdr[7] != '0' && hdr[7] != '1') || hdr[8] != ' ' ||
     !isdigit((unsigned char)hdr[9]) || !isdigit((unsigned char)hdr[10]) ||
     !isdigit((unsigned char)hdr[11])) {
    buffer_free(&raw);
    return -1;
  }
  *out_status = (hdr[9] - '0') * 100 + (hdr[10] - '0') * 10 + (hdr[11] - '0');

  int has_content_length = 0;
  size_t content_length = 0;
  int is_chunked = 0;

  size_t line_pos = first_crlf + 2u;
  while(line_pos + 2u <= header_end) {
    size_t line_end = line_pos;
    while(line_end + 1u < header_end && !(hdr[line_end] == '\r' && hdr[line_end + 1u] == '\n')) {
      line_end++;
    }
    size_t line_len = line_end - line_pos;
    if(line_len == 0) break;
    const char *line = hdr + line_pos;
    if(ci_header_prefix(line, line_len, "content-length:")) {
      const char *v = line + 15u;
      const char *vend = line + line_len;
      while(v < vend && (*v == ' ' || *v == '\t')) v++;
      while(vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
      if(v == vend) {
        buffer_free(&raw);
        return -1;
      }
      size_t parsed_len = 0;
      for(const char *p = v; p < vend; ++p) {
        if(*p < '0' || *p > '9') {
          buffer_free(&raw);
          return -1;
        }
        unsigned d = (unsigned)(*p - '0');
        if(parsed_len > (FETCH_BODY_CAP - d) / 10u) {
          buffer_free(&raw);
          return -1;
        }
        parsed_len = parsed_len * 10u + d;
      }
      if(has_content_length && parsed_len != content_length) {
        buffer_free(&raw);
        return -1;
      }
      has_content_length = 1;
      content_length = parsed_len;
    } else if(ci_header_prefix(line, line_len, "transfer-encoding:")) {
      const char *v = line + 18u;
      size_t vlen = line_len - 18u;
      if(ci_contains_token(v, vlen, "chunked")) {
        is_chunked = 1;
      }
    } else if(ci_header_prefix(line, line_len, "location:") && out_location && location_cap > 0) {
      const char *v = line + 9u;
      const char *vend = line + line_len;
      while(v < vend && (*v == ' ' || *v == '\t')) v++;
      while(vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
      size_t vlen = (size_t)(vend - v);
      if(vlen >= location_cap) {
        buffer_free(&raw);
        return -1;
      }
      memcpy(out_location, v, vlen);
      out_location[vlen] = '\0';
    }
    line_pos = line_end + 2u;
  }

  int bodyless_status = (*out_status >= 100 && *out_status < 200) ||
                        *out_status == 204 || *out_status == 304;
  int chunked_done = 0;

  /* Read remaining body bytes from fd */
  while(!bodyless_status) {
    size_t have_body = raw.len - header_end;
    if(is_chunked) {
      buffer_clear(out_body);
      int crc = decode_chunked_body(raw.data + header_end, have_body, out_body);
      if(crc == 0) {
        chunked_done = 1;
        break;
      }
      if(crc < 0) {
        buffer_free(&raw);
        buffer_clear(out_body);
        return -1;
      }
    } else if(has_content_length && have_body >= content_length) {
      break;
    }
    if(have_body > FETCH_BODY_CAP + 4096u) {
      buffer_free(&raw);
      buffer_clear(out_body);
      return -1;
    }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr = poll(&pfd, 1u, 8000);
    if(pr < 0 && errno == EINTR) continue;
    if(pr <= 0) {
      buffer_free(&raw);
      buffer_clear(out_body);
      return -1;
    }
    char chunk[8192];
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n < 0) {
      buffer_free(&raw);
      buffer_clear(out_body);
      return -1;
    }
    if(n == 0) break;
    buffer_append(&raw, chunk, (size_t)n);
    if(raw.truncated) {
      buffer_free(&raw);
      buffer_clear(out_body);
      return -1;
    }
  }

  const char *body_ptr = raw.data + header_end;
  size_t body_avail = raw.len - header_end;
  if(bodyless_status) {
    buffer_clear(out_body);
  } else if(is_chunked) {
    if(!chunked_done) {
      buffer_clear(out_body);
      if(decode_chunked_body(body_ptr, body_avail, out_body) != 0) {
        buffer_free(&raw);
        buffer_clear(out_body);
        return -1;
      }
    }
  } else if(has_content_length) {
    if(body_avail < content_length || content_length > FETCH_BODY_CAP) {
      buffer_free(&raw);
      return -1;
    }
    buffer_append(out_body, body_ptr, content_length);
  } else {
    if(body_avail > FETCH_BODY_CAP) {
      buffer_free(&raw);
      return -1;
    }
    buffer_append(out_body, body_ptr, body_avail);
  }
  buffer_free(&raw);
  if(out_body->truncated) {
    buffer_clear(out_body);
    return -1;
  }
  if(!out_body->data) {
    buffer_append(out_body, "", 0u);
  }
  return 0;
}

static int fetch_native_http_hop(const char *url,
                                 const FetchTarget *target,
                                 const struct sockaddr_storage *addr,
                                 socklen_t addr_len,
                                 unsigned time_seconds,
                                 Buffer *body,
                                 int *status,
                                 char redirect[FETCH_URL_CAP]) {
  int fd = socket(addr->ss_family, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int crc = connect(fd, (const struct sockaddr *)addr, addr_len);
  if(crc != 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }
  if(crc != 0) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int pr = poll(&pfd, 1u, (int)time_seconds * 1000);
    if(pr <= 0) {
      close(fd);
      return -1;
    }
    int so_err = 0;
    socklen_t slen = sizeof(so_err);
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) != 0 || so_err != 0) {
      close(fd);
      return -1;
    }
  }

  char raw_loc[FETCH_URL_CAP];
  raw_loc[0] = '\0';
  int xrc = http_client_exchange_fd(fd, target->host, (uint16_t)target->port_num, 80,
                                    target->path, status, raw_loc, sizeof(raw_loc), body);
  close(fd);
  if(xrc != 0) return -1;
  redirect[0] = '\0';
  if(*status >= 300 && *status < 400 && raw_loc[0] != '\0') {
    if(http_resolve_redirect_url(url, raw_loc, redirect, FETCH_URL_CAP) != 0) {
      return -1;
    }
  }
  return 0;
}

static int fetch_one(const char *url, const FetchTarget *target, const char *ip,
                     unsigned time_seconds, Buffer *body, int *status,
                     char redirect[FETCH_URL_CAP]) {
  char resolve_arg[512];
  if(!target->literal) {
    int n = snprintf(resolve_arg, sizeof(resolve_arg), "%s:%s:%s%s%s",
                     target->host, target->port, strchr(ip, ':') ? "[" : "", ip,
                     strchr(ip, ':') ? "]" : "");
    if(n < 0 || (size_t)n >= sizeof(resolve_arg)) return -1;
  }
  char time_arg[16];
  snprintf(time_arg, sizeof(time_arg), "%u", time_seconds);

  char *argv[32];
  size_t argc = 0;
  argv[argc++] = "curl";
  argv[argc++] = "--disable";
  argv[argc++] = "--silent";
  argv[argc++] = "--fail";
  argv[argc++] = "--globoff";
  argv[argc++] = "--proto"; argv[argc++] = "=http,https";
  argv[argc++] = "--noproxy"; argv[argc++] = "*";
  argv[argc++] = "--proxy"; argv[argc++] = "";
  argv[argc++] = "--max-time"; argv[argc++] = time_arg;
  argv[argc++] = "--max-filesize"; argv[argc++] = "8388608";
  argv[argc++] = "--user-agent"; argv[argc++] = "Oyez/2.0 (Pure First-Party C99 Intelligence Engine)";
  if(!target->literal) {
    argv[argc++] = "--resolve";
    argv[argc++] = resolve_arg;
  }
  argv[argc++] = "--output"; argv[argc++] = "-";
  argv[argc++] = "--write-out"; argv[argc++] = "%{stderr}%{http_code} %{redirect_url}";
  argv[argc++] = "--url"; argv[argc++] = (char *)url;
  argv[argc] = NULL;

  int body_fd[2], meta_fd[2];
  if(pipe(body_fd) != 0) return -1;
  if(pipe(meta_fd) != 0) {
    close(body_fd[0]); close(body_fd[1]);
    return -1;
  }
  posix_spawn_file_actions_t actions;
  int action_rc = posix_spawn_file_actions_init(&actions);
  int actions_initialized = (action_rc == 0);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_adddup2(&actions, body_fd[1], STDOUT_FILENO);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_adddup2(&actions, meta_fd[1], STDERR_FILENO);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_addclose(&actions, body_fd[0]);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_addclose(&actions, meta_fd[0]);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_addclose(&actions, body_fd[1]);
  if(action_rc == 0) action_rc = posix_spawn_file_actions_addclose(&actions, meta_fd[1]);
  pid_t child = -1;
  int spawn_rc = action_rc ? action_rc : posix_spawnp(&child, "curl", &actions, NULL, argv, environ);
  if(actions_initialized) posix_spawn_file_actions_destroy(&actions);
  close(body_fd[1]);
  close(meta_fd[1]);
  if(spawn_rc != 0) {
    close(body_fd[0]); close(meta_fd[0]);
    return -1;
  }

  char meta[FETCH_URL_CAP + 8u];
  size_t meta_len = 0;
  int failed = 0;
  int open_body = 1, open_meta = 1;
  while(open_body || open_meta) {
    struct pollfd fds[2] = {
        {.fd = open_body ? body_fd[0] : -1, .events = POLLIN},
        {.fd = open_meta ? meta_fd[0] : -1, .events = POLLIN}};
    int ready = poll(fds, 2u, (int)time_seconds * 1000 + 1000);
    if(ready < 0 && errno == EINTR) continue;
    if(ready <= 0) { failed = 1; break; }
    for(int i = 0; i < 2; ++i) {
      if(fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      char chunk[8192];
      ssize_t n = read(fds[i].fd, chunk, sizeof(chunk));
      if(n < 0 && errno == EINTR) continue;
      if(n < 0) { failed = 1; break; }
      if(n == 0) {
        close(fds[i].fd);
        if(i == 0) open_body = 0;
        else open_meta = 0;
      } else if(i == 0) {
        if((size_t)n > FETCH_BODY_CAP - body->len) { failed = 1; break; }
        buffer_append(body, chunk, (size_t)n);
        if(body->truncated) { failed = 1; break; }
      } else {
        if((size_t)n > sizeof(meta) - 1u - meta_len) { failed = 1; break; }
        memcpy(meta + meta_len, chunk, (size_t)n);
        meta_len += (size_t)n;
      }
    }
    if(failed) break;
  }
  if(open_body) close(body_fd[0]);
  if(open_meta) close(meta_fd[0]);
  if(failed) kill(child, SIGTERM);
  int child_status = 0;
  while(waitpid(child, &child_status, 0) < 0) {
    if(errno != EINTR) return -1;
  }
  if(failed || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 || meta_len < 4u) return -1;
  meta[meta_len] = '\0';
  if(!isdigit((unsigned char)meta[0]) || !isdigit((unsigned char)meta[1]) ||
     !isdigit((unsigned char)meta[2]) || meta[3] != ' ') return -1;
  *status = (meta[0] - '0') * 100 + (meta[1] - '0') * 10 + (meta[2] - '0');
  size_t redirect_len = meta_len - 4u;
  if(redirect_len >= FETCH_URL_CAP) return -1;
  memcpy(redirect, meta + 4u, redirect_len + 1u);
  return 0;
}

int http_fetch_url(const char *url, Buffer *out) {
  if(!out) return -1;
  buffer_init(out);
  if(!url || strlen(url) >= FETCH_URL_CAP) return -1;
  char current[FETCH_URL_CAP];
  strcpy(current, url);
  int64_t deadline = monotonic_ms() + 12000;

  for(unsigned hop = 0; hop <= 5u; ++hop) {
    FetchTarget target;
    char ip[INET6_ADDRSTRLEN];
    struct sockaddr_storage addr;
    socklen_t addr_len = 0;
    if(parse_fetch_url(current, &target) != 0 ||
       resolve_public_endpoint(&target, ip, &addr, &addr_len) != 0) break;
    int64_t left = deadline - monotonic_ms();
    if(left <= 0) break;
    unsigned seconds = (unsigned)((left + 999) / 1000);
    if(seconds == 0u) seconds = 1u;

    buffer_clear(out);
    int status = 0;
    char redirect[FETCH_URL_CAP];
    int hop_rc = -1;
    if(!target.is_https && addr_len > 0 && getenv("OYEZ_TEST_CALLS") == NULL) {
      hop_rc = fetch_native_http_hop(current, &target, &addr, addr_len, seconds, out, &status, redirect);
    } else {
      hop_rc = fetch_one(current, &target, ip, seconds, out, &status, redirect);
    }
    if(hop_rc != 0) break;
    if(status >= 200 && status < 300 && out->len > 0u) return 0;
    if(status < 300 || status >= 400 || redirect[0] == '\0' || hop == 5u) break;
    char resolved[FETCH_URL_CAP];
    if(http_resolve_redirect_url(current, redirect, resolved, sizeof(resolved)) != 0) break;
    strcpy(current, resolved);
  }
  buffer_free(out);
  return -1;
}
