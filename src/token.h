#ifndef OYEZ_TOKEN_H
#define OYEZ_TOKEN_H

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOKEN_BASE 256u
#define TOKEN_MAX_VOCAB 131072u
#define TOKEN_MAX_SPECIAL 64u
#define TOKEN_MAX_CORPUS 0xFFFFFFFEu

typedef struct {
    uint32_t vocab;
    uint32_t n_merges;
    uint32_t n_special;
    uint32_t *merge_left;
    uint32_t *merge_right;
    char **special;

    unsigned char *piece_pool;
    size_t *piece_off;
    void *rank_index;
} Tokenizer;

typedef struct {
    uint32_t target_vocab;
    uint32_t min_frequency;
} TokenTrainConfig;

TokenTrainConfig token_train_config_default(void);

bool token_train(Tokenizer *t, const void *data, size_t length,
                 const uint32_t *seg_offsets, size_t seg_count,
                 const char *const *special, size_t n_special,
                 const TokenTrainConfig *cfg, char error[256]);

void token_free(Tokenizer *t);
bool token_valid(const Tokenizer *t);
uint32_t token_vocab(const Tokenizer *t);

bool token_encode(const Tokenizer *t, const void *bytes, size_t length,
                  uint32_t **out, size_t *n_out, char error[256]);

bool token_merge_ids(const Tokenizer *t, const uint32_t *symbols, size_t count,
                     uint32_t **out, size_t *n_out, char error[256]);

bool token_decode(const Tokenizer *t, const uint32_t *ids, size_t count,
                  Buffer *out, char error[256]);

bool token_special_id(const Tokenizer *t, const char *name, uint32_t *id);
const char *token_id_name(const Tokenizer *t, uint32_t id);

bool token_save(const Tokenizer *t, Buffer *out);
bool token_load(Tokenizer *t, const void *bytes, size_t length, char error[256]);

#ifdef __cplusplus
}
#endif

#endif
