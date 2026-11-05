#ifndef OYEZ_M1_TOKENIZER_H
#define OYEZ_M1_TOKENIZER_H

#include "token.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  M1_INDENT_BASE = 256,
  M1_INDENT_COUNT = 26,
  M1_PRIMITIVE_BASE = M1_INDENT_BASE + M1_INDENT_COUNT,
  M1_PRIMITIVE_COUNT = 200,
  M1_LANDMARK_BASE = M1_PRIMITIVE_BASE + M1_PRIMITIVE_COUNT,
  M1_LANDMARK_COUNT = 144,
  M1_CONTROL_BASE = M1_LANDMARK_BASE + M1_LANDMARK_COUNT,
  M1_CONTROL_COUNT = 16,
  M1_FIXED_VOCAB_SIZE = M1_CONTROL_BASE + M1_CONTROL_COUNT,
  M1_DEFAULT_VOCAB_SIZE = 32768
};

typedef char oyez_c99_m1_fixed_vocab_assert[(M1_FIXED_VOCAB_SIZE == 642) ? 1 : -1];

typedef enum {
  M1_CTRL_BOS = M1_CONTROL_BASE + 0,
  M1_CTRL_EOS = M1_CONTROL_BASE + 1,
  M1_CTRL_PAD = M1_CONTROL_BASE + 2,
  M1_CTRL_SYSTEM = M1_CONTROL_BASE + 3,
  M1_CTRL_USER = M1_CONTROL_BASE + 4,
  M1_CTRL_ASSISTANT = M1_CONTROL_BASE + 5,
  M1_CTRL_THOUGHT = M1_CONTROL_BASE + 6,
  M1_CTRL_FINAL = M1_CONTROL_BASE + 7,
  M1_CTRL_TOOL_CALL = M1_CONTROL_BASE + 8,
  M1_CTRL_TOOL_RESULT = M1_CONTROL_BASE + 9,
  M1_CTRL_FIM_PRE = M1_CONTROL_BASE + 10,
  M1_CTRL_FIM_SUF = M1_CONTROL_BASE + 11,
  M1_CTRL_DISPATCH = M1_CONTROL_BASE + 12,
  M1_CTRL_CITATION = M1_CONTROL_BASE + 13,
  M1_CTRL_TICKER = M1_CONTROL_BASE + 14,
  M1_CTRL_ADVISORY = M1_CONTROL_BASE + 15,
  M1_CTRL_FIM_MID = M1_LANDMARK_BASE + 143
} M1ControlToken;

typedef struct {
  Tokenizer bpe;
  uint32_t target_vocab_size;
  char sha256[65];
} M1Tokenizer;

typedef struct {
  uint32_t *tokens;
  uint8_t *loss_mask;
  size_t length;
  size_t capacity;
} M1Sequence;

void m1_tokenizer_init(M1Tokenizer *tokenizer);
void m1_tokenizer_free(M1Tokenizer *tokenizer);
int m1_tokenizer_train(M1Tokenizer *tokenizer, const uint8_t *data, size_t len, uint32_t vocab_size);
uint32_t m1_tokenizer_vocab_size(const M1Tokenizer *tokenizer);
const char *m1_tokenizer_control_name(uint32_t token);
int m1_tokenizer_encode(const M1Tokenizer *tokenizer,
                        const uint8_t *data,
                        size_t len,
                        uint32_t **out_tokens,
                        size_t *out_count);
int m1_tokenizer_decode(const M1Tokenizer *tokenizer,
                        const uint32_t *tokens,
                        size_t count,
                        uint8_t **out_bytes,
                        size_t *out_len);
int m1_tokenizer_save(M1Tokenizer *tokenizer, const char *path);
int m1_tokenizer_load(M1Tokenizer *tokenizer, const char *path);

void m1_sequence_init(M1Sequence *seq);
void m1_sequence_free(M1Sequence *seq);
int m1_sequence_push_token(M1Sequence *seq, uint32_t token, uint8_t loss_mask);
int m1_sequence_append_text(M1Sequence *seq,
                            const M1Tokenizer *tokenizer,
                            const char *text,
                            size_t len,
                            uint8_t loss_mask);
int m1_sequence_begin(M1Sequence *seq);
int m1_sequence_end(M1Sequence *seq);
int m1_sequence_message(M1Sequence *seq,
                        const M1Tokenizer *tokenizer,
                        M1ControlToken role_token,
                        const char *text,
                        size_t len,
                        uint8_t loss_mask);
int m1_sequence_assistant(M1Sequence *seq,
                          const M1Tokenizer *tokenizer,
                          const char *thought,
                          size_t thought_len,
                          const char *final_text,
                          size_t final_len,
                          uint8_t include_thought_loss);
uint64_t m1_structural_sketch64(const char *text,
                                size_t len,
                                uint32_t *out_landmark_hits);

#ifdef __cplusplus
}
#endif

#endif
