#ifndef OYEZ_WIRE_H
#define OYEZ_WIRE_H

#include "core.h"
#include <assert.h>
#include <float.h>
#include <string.h>

static inline void wire_append_le(Buffer *out, uint64_t value, unsigned bytes)
{
    assert(bytes > 0 && bytes <= 8);
    unsigned char data[8];
    for (unsigned i = 0; i < bytes; ++i) {
        data[i] = (unsigned char)(value >> (8 * i));
    }
    buffer_append(out, data, bytes);
}

static inline uint64_t wire_take_le(const unsigned char **cursor, unsigned bytes)
{
    assert(bytes > 0 && bytes <= 8);
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        value |= (uint64_t)(*cursor)[i] << (8 * i);
    }
    *cursor += bytes;
    return value;
}

static inline void wire_append_u32(Buffer *out, uint32_t value)
{
    wire_append_le(out, value, 4);
}

static inline void wire_append_u64(Buffer *out, uint64_t value)
{
    wire_append_le(out, value, 8);
}

OYEZ_C99_STATIC_ASSERT(
    sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
    wire_ieee754_binary64);

static inline void wire_append_f64(Buffer *out, double value)
{
    wire_append_u64(out, f64_to_bits(value));
}

static inline bool wire_read_le(const unsigned char *data, size_t length,
                                size_t *offset, unsigned bytes, uint64_t *out)
{
    if (!data || !offset || !out || !bytes || bytes > 8 ||
        *offset > length || bytes > length - *offset) return false;
    const unsigned char *cursor = data + *offset;
    const uint64_t value = wire_take_le(&cursor, bytes);
    *offset += bytes;
    *out = value;
    return true;
}

static inline bool wire_read_u32(const unsigned char *data, size_t length,
                                 size_t *offset, uint32_t *out)
{
    uint64_t value;
    if (!out || !wire_read_le(data, length, offset, 4, &value)) return false;
    *out = (uint32_t)value;
    return true;
}

static inline bool wire_read_u64(const unsigned char *data, size_t length,
                                 size_t *offset, uint64_t *out)
{
    return wire_read_le(data, length, offset, 8, out);
}

static inline bool wire_read_f64(const unsigned char *data, size_t length,
                                 size_t *offset, double *out)
{
    uint64_t bits;
    if (!out || !wire_read_u64(data, length, offset, &bits)) return false;
    memcpy(out, &bits, sizeof(bits));
    return true;
}

#endif
