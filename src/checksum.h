#ifndef OYEZ_CHECKSUM_H
#define OYEZ_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

uint32_t checksum_crc32(const void *bytes, size_t length);
uint32_t checksum_crc32_update(uint32_t previous, const void *bytes, size_t length);

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} Sha256Digest;

void sha256_begin(Sha256Digest *digest);
bool sha256_update(Sha256Digest *digest, const void *bytes, size_t length);
void sha256_end(const Sha256Digest *digest, char hex[65]);
void sha256_hex(const void *bytes, size_t length, char hex[65]);
bool sha256_valid_hex(const char hex[65]);

#endif
