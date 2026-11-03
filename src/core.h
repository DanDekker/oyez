#ifndef OYEZ_CORE_H
#define OYEZ_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
    bool fixed;
    bool truncated;
} Buffer;

void *allocate(size_t count, size_t size);
static inline void buffer_init(Buffer *buffer)
{
    if (!buffer) return;
    buffer->data = NULL;
    buffer->len = 0;
    buffer->capacity = 0;
    buffer->fixed = false;
    buffer->truncated = false;
}
void buffer_attach(Buffer *buffer, char *data, size_t capacity);
void buffer_append(Buffer *buffer, const void *data, size_t length);
void buffer_text(Buffer *buffer, const char *text);
void buffer_format(Buffer *buffer, const char *format, ...);
void buffer_clear(Buffer *buffer);
void buffer_free(Buffer *buffer);
void json_string(Buffer *buffer, const char *text, size_t length);
void json_cstr(Buffer *buffer, const char *text);

int64_t monotonic_ms(void);
int64_t monotonic_ns(void);
int64_t wall_time_ms(void);

static inline uint64_t mix64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53ULL;
    x ^= x >> 33;
    return x;
}

static inline uint64_t fnv1a64(const void *bytes, size_t len)
{
    const unsigned char *p = (const unsigned char *)bytes;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static inline uint64_t fnv1a64_cstr(const char *s)
{
    if (!s) return 0xcbf29ce484222325ULL;
    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = 0xcbf29ce484222325ULL;
    while (*p) {
        unsigned char c = *p++;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        h ^= (uint64_t)c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static inline unsigned popcnt64(uint64_t x)
{
    return (unsigned)__builtin_popcountll(x);
}

static inline unsigned ctz64(uint64_t x)
{
    return x ? (unsigned)__builtin_ctzll(x) : 64u;
}

static inline unsigned clz64(uint64_t x)
{
    return x ? (unsigned)__builtin_clzll(x) : 64u;
}

static inline uint64_t rotl64(uint64_t x, unsigned k)
{
    return (x << (k & 63u)) | (x >> ((64u - k) & 63u));
}

static inline uint64_t wyhash64(uint64_t a, uint64_t b)
{
    a ^= 0x2d358dccaa6c78a5ULL;
    b ^= 0x8bb84b93962eacc9ULL;
    __uint128_t r = (__uint128_t)a * (__uint128_t)b;
    return mix64((uint64_t)r ^ (uint64_t)(r >> 64));
}

#endif
