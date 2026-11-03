#include "http_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
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

typedef struct {
  char host[256];
  char port[6];
  int literal;
} FetchTarget;

static int parse_fetch_url(const char *url, FetchTarget *target) {
  if(!url || !target) return -1;
  size_t len = strlen(url);
  if(len == 0 || len >= FETCH_URL_CAP) return -1;
  const char *authority = NULL;
  unsigned port = 0;
  if(strncmp(url, "http://", 7u) == 0) {
    authority = url + 7u;
    port = 80u;
  } else if(strncmp(url, "https://", 8u) == 0) {
    authority = url + 8u;
    port = 443u;
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
  if(*authority == '[') {
    const char *close = memchr(authority, ']', (size_t)(end - authority));
    if(!close || close == authority + 1u || (size_t)(close - authority - 1u) >= sizeof(target->host)) return -1;
    size_t host_len = (size_t)(close - authority - 1u);
    memcpy(target->host, authority + 1u, host_len);
    target->host[host_len] = '\0';
    struct in6_addr addr6;
    if(inet_pton(AF_INET6, target->host, &addr6) != 1) return -1;
    target->literal = 1;
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
  snprintf(target->port, sizeof(target->port), "%u", port);
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

static int resolve_public_ip(const FetchTarget *target, char ip[INET6_ADDRSTRLEN]) {
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
      chosen_family = ai->ai_family;
      found = 1;
    }
  }
  freeaddrinfo(results);
  return found == 1 ? 0 : -1;
}

int http_url_is_public(const char *url) {
  FetchTarget target;
  char ip[INET6_ADDRSTRLEN];
  return parse_fetch_url(url, &target) == 0 && resolve_public_ip(&target, ip) == 0;
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
  argv[argc++] = "--user-agent"; argv[argc++] = "Oyez/1.0 (+https://github.com/dandekker/oyez)";
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
    if(parse_fetch_url(current, &target) != 0 || resolve_public_ip(&target, ip) != 0) break;
    int64_t left = deadline - monotonic_ms();
    if(left <= 0) break;
    unsigned seconds = (unsigned)((left + 999) / 1000);
    if(seconds == 0u) seconds = 1u;

    buffer_clear(out);
    int status = 0;
    char redirect[FETCH_URL_CAP];
    if(fetch_one(current, &target, ip, seconds, out, &status, redirect) != 0) break;
    if(status >= 200 && status < 300 && out->len > 0u) return 0;
    if(status < 300 || status >= 400 || redirect[0] == '\0' || hop == 5u) break;
    strcpy(current, redirect);
  }
  buffer_free(out);
  return -1;
}
