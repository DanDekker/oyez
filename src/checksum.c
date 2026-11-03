#include "checksum.h"

#include <stdio.h>
#include <string.h>

uint32_t checksum_crc32_update(uint32_t previous, const void *bytes, size_t length)
{
    uint32_t table[16];
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t value = i;
        for (unsigned bit = 0; bit < 4; ++bit) {
            value = (value >> 1) ^ ((value & 1) ? UINT32_C(0xedb88320) : 0);
        }
        table[i] = value;
    }
    uint32_t value = ~previous;
    const unsigned char *p = (const unsigned char *)bytes;
    for (size_t i = 0; i < length; ++i) {
        value ^= p[i];
        value = (value >> 4) ^ table[value & 15];
        value = (value >> 4) ^ table[value & 15];
    }
    return ~value;
}

uint32_t checksum_crc32(const void *bytes, size_t length)
{
    return checksum_crc32_update(0, bytes, length);
}

static uint32_t rotate32(uint32_t n, unsigned by)
{
    return (n >> by) | (n << (32 - by));
}

static void sha256_compress(uint32_t state[8], const unsigned char bytes[64])
{
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)bytes[i * 4] << 24) |
                   ((uint32_t)bytes[i * 4 + 1] << 16) |
                   ((uint32_t)bytes[i * 4 + 2] << 8) |
                   (uint32_t)bytes[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t a = words[i - 15];
        const uint32_t b = words[i - 2];
        words[i] = words[i - 16] +
                   (rotate32(a, 7) ^ rotate32(a, 18) ^ (a >> 3)) +
                   words[i - 7] +
                   (rotate32(b, 17) ^ rotate32(b, 19) ^ (b >> 10));
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t first = h + (rotate32(e, 6) ^ rotate32(e, 11) ^ rotate32(e, 25)) +
                               ((e & f) ^ (~e & g)) + constants[i] + words[i];
        const uint32_t second = (rotate32(a, 2) ^ rotate32(a, 13) ^ rotate32(a, 22)) +
                                ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_begin(Sha256Digest *digest)
{
    *digest = (Sha256Digest){
        .state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        }
    };
}

bool sha256_update(Sha256Digest *digest, const void *bytes, size_t length)
{
    if (!digest || digest->used >= 64 || digest->bytes > UINT64_MAX / 8 ||
        (!bytes && length) || length > UINT64_MAX / 8 - digest->bytes) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)bytes;
    size_t at = 0;
    digest->bytes += length;
    if (digest->used) {
        const size_t take = length < 64 - digest->used ? length : 64 - digest->used;
        if (take) memcpy(digest->block + digest->used, p, take);
        digest->used += take;
        at = take;
        if (digest->used == 64) {
            sha256_compress(digest->state, digest->block);
            digest->used = 0;
        }
    }
    while (length - at >= 64) {
        sha256_compress(digest->state, p + at);
        at += 64;
    }
    if (length > at) {
        digest->used = length - at;
        memcpy(digest->block, p + at, digest->used);
    }
    return true;
}

void sha256_end(const Sha256Digest *digest, char hex[65])
{
    uint32_t state[8];
    memcpy(state, digest->state, sizeof(state));
    unsigned char last[128] = {0};
    const size_t remaining = digest->used;
    if (remaining) memcpy(last, digest->block, remaining);
    last[remaining] = 0x80;
    const size_t total = remaining < 56 ? 64 : 128;
    const uint64_t bits = digest->bytes * 8;
    for (unsigned i = 0; i < 8; ++i) {
        last[total - 1 - i] = (unsigned char)(bits >> (i * 8));
    }
    sha256_compress(state, last);
    if (total == 128) sha256_compress(state, last + 64);
    for (unsigned i = 0; i < 8; ++i) {
        (void)snprintf(hex + i * 8, 9, "%08x", state[i]);
    }
    hex[64] = '\0';
}

void sha256_hex(const void *bytes, size_t length, char hex[65])
{
    Sha256Digest digest;
    sha256_begin(&digest);
    if (!sha256_update(&digest, bytes, length)) {
        hex[0] = '\0';
        return;
    }
    sha256_end(&digest, hex);
}

bool sha256_valid_hex(const char hex[65])
{
    if (!hex) return false;
    for (size_t i = 0; i < 64; ++i) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return hex[64] == '\0';
}
