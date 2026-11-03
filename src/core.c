#include "core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *allocate(size_t count, size_t size)
{
    if (!count || !size) return calloc(1, 1);
    return calloc(count, size);
}

void buffer_attach(Buffer *buffer, char *data, size_t capacity)
{
    if (!buffer) return;
    *buffer = (Buffer){
        .data = data,
        .len = 0,
        .capacity = capacity,
        .fixed = true,
        .truncated = false,
    };
    if (data && capacity > 0) data[0] = '\0';
}

static bool buffer_reserve(Buffer *buffer, size_t extra)
{
    if (!buffer || buffer->truncated) return false;
    if (extra > SIZE_MAX - buffer->len - 1) {
        buffer->truncated = true;
        return false;
    }
    size_t needed = buffer->len + extra + 1;
    if (needed <= buffer->capacity) return true;
    if (buffer->fixed) {
        buffer->truncated = true;
        return false;
    }
    size_t cap = buffer->capacity ? buffer->capacity : 256;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    char *next = realloc(buffer->data, cap);
    if (!next) {
        buffer->truncated = true;
        return false;
    }
    buffer->data = next;
    buffer->capacity = cap;
    return true;
}

void buffer_append(Buffer *buffer, const void *data, size_t length)
{
    if (!buffer || buffer->truncated) return;
    if (length == 0) {
        if (!buffer->data && !buffer_reserve(buffer, 0)) return;
        if (buffer->data) buffer->data[buffer->len] = '\0';
        return;
    }
    if (!data || !buffer_reserve(buffer, length)) return;
    memcpy(buffer->data + buffer->len, data, length);
    buffer->len += length;
    buffer->data[buffer->len] = '\0';
}

void buffer_text(Buffer *buffer, const char *text)
{
    if (!text) return;
    buffer_append(buffer, text, strlen(text));
}

void buffer_format(Buffer *buffer, const char *format, ...)
{
    if (!buffer || !format || buffer->truncated) return;
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (n < 0 || !buffer_reserve(buffer, (size_t)n)) {
        va_end(copy);
        buffer->truncated = true;
        return;
    }
    vsnprintf(buffer->data + buffer->len, (size_t)n + 1, format, copy);
    va_end(copy);
    buffer->len += (size_t)n;
}

void buffer_clear(Buffer *buffer)
{
    if (!buffer) return;
    buffer->len = 0;
    buffer->truncated = false;
    if (buffer->data && buffer->capacity > 0) buffer->data[0] = '\0';
}

void buffer_free(Buffer *buffer)
{
    if (!buffer) return;
    if (!buffer->fixed) free(buffer->data);
    *buffer = (Buffer){0};
}

void json_string(Buffer *buffer, const char *text, size_t length)
{
    buffer_append(buffer, "\"", 1);
    const unsigned char *p = (const unsigned char *)text;
    for (size_t i = 0; i < length && p; ++i) {
        unsigned char c = p[i];
        switch (c) {
        case '"':  buffer_append(buffer, "\\\"", 2); break;
        case '\\': buffer_append(buffer, "\\\\", 2); break;
        case '\b': buffer_append(buffer, "\\b", 2); break;
        case '\f': buffer_append(buffer, "\\f", 2); break;
        case '\n': buffer_append(buffer, "\\n", 2); break;
        case '\r': buffer_append(buffer, "\\r", 2); break;
        case '\t': buffer_append(buffer, "\\t", 2); break;
        default:
            if (c < 0x20) {
                buffer_format(buffer, "\\u%04x", (unsigned)c);
            } else {
                buffer_append(buffer, &c, 1);
            }
            break;
        }
    }
    buffer_append(buffer, "\"", 1);
}

void json_cstr(Buffer *buffer, const char *text)
{
    if (!text) {
        buffer_text(buffer, "\"\"");
        return;
    }
    json_string(buffer, text, strlen(text));
}

int64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

int64_t monotonic_ms(void)
{
    return monotonic_ns() / 1000000LL;
}

int64_t wall_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}
