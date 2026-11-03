#include "m1_tokenizer.h"

#include "checksum.h"
#include "core.h"
#include "wire.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *text;
  uint16_t len;
  uint8_t boundary;
} M1FixedPiece;

enum {
  M1_BOUNDARY_NONE = 0,
  M1_BOUNDARY_IDENT = 1,
  M1_BOUNDARY_HEADER = 2
};

#define PIECE(lit, mode) {lit, (uint16_t)(sizeof(lit) - 1u), mode}

static const M1FixedPiece k_indent_pieces[M1_INDENT_COUNT] = {
    PIECE("\n ", M1_BOUNDARY_NONE),
    PIECE("\n  ", M1_BOUNDARY_NONE),
    PIECE("\n   ", M1_BOUNDARY_NONE),
    PIECE("\n    ", M1_BOUNDARY_NONE),
    PIECE("\n     ", M1_BOUNDARY_NONE),
    PIECE("\n      ", M1_BOUNDARY_NONE),
    PIECE("\n       ", M1_BOUNDARY_NONE),
    PIECE("\n        ", M1_BOUNDARY_NONE),
    PIECE("\n         ", M1_BOUNDARY_NONE),
    PIECE("\n          ", M1_BOUNDARY_NONE),
    PIECE("\n           ", M1_BOUNDARY_NONE),
    PIECE("\n            ", M1_BOUNDARY_NONE),
    PIECE("\n             ", M1_BOUNDARY_NONE),
    PIECE("\n              ", M1_BOUNDARY_NONE),
    PIECE("\n               ", M1_BOUNDARY_NONE),
    PIECE("\n                ", M1_BOUNDARY_NONE),
    PIECE("\n\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t\t\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\n\t\t\t\t\t\t\t\t", M1_BOUNDARY_NONE),
    PIECE("\r\n", M1_BOUNDARY_NONE),
    PIECE("\n\n", M1_BOUNDARY_NONE),
};

static const M1FixedPiece k_primitive_pieces[M1_PRIMITIVE_COUNT] = {
    PIECE("/**", M1_BOUNDARY_NONE),       PIECE("///", M1_BOUNDARY_NONE),
    PIECE("...", M1_BOUNDARY_NONE),       PIECE("<<=", M1_BOUNDARY_NONE),
    PIECE(">>=", M1_BOUNDARY_NONE),       PIECE("===", M1_BOUNDARY_NONE),
    PIECE("!==", M1_BOUNDARY_NONE),       PIECE("<=>", M1_BOUNDARY_NONE),
    PIECE("->", M1_BOUNDARY_NONE),        PIECE("=>", M1_BOUNDARY_NONE),
    PIECE("::", M1_BOUNDARY_NONE),        PIECE(":=", M1_BOUNDARY_NONE),
    PIECE("==", M1_BOUNDARY_NONE),        PIECE("!=", M1_BOUNDARY_NONE),
    PIECE("<=", M1_BOUNDARY_NONE),        PIECE(">=", M1_BOUNDARY_NONE),
    PIECE("&&", M1_BOUNDARY_NONE),        PIECE("||", M1_BOUNDARY_NONE),
    PIECE("<<", M1_BOUNDARY_NONE),        PIECE(">>", M1_BOUNDARY_NONE),
    PIECE("+=", M1_BOUNDARY_NONE),        PIECE("-=", M1_BOUNDARY_NONE),
    PIECE("*=", M1_BOUNDARY_NONE),        PIECE("/=", M1_BOUNDARY_NONE),
    PIECE("%=", M1_BOUNDARY_NONE),        PIECE("&=", M1_BOUNDARY_NONE),
    PIECE("|=", M1_BOUNDARY_NONE),        PIECE("^=", M1_BOUNDARY_NONE),
    PIECE("++", M1_BOUNDARY_NONE),        PIECE("--", M1_BOUNDARY_NONE),
    PIECE("//", M1_BOUNDARY_NONE),        PIECE("/*", M1_BOUNDARY_NONE),
    PIECE("*/", M1_BOUNDARY_NONE),        PIECE("#include", M1_BOUNDARY_IDENT),
    PIECE("#define", M1_BOUNDARY_IDENT),  PIECE("#ifndef", M1_BOUNDARY_IDENT),
    PIECE("#ifdef", M1_BOUNDARY_IDENT),   PIECE("#endif", M1_BOUNDARY_IDENT),
    PIECE("#pragma", M1_BOUNDARY_IDENT),  PIECE("typedef", M1_BOUNDARY_IDENT),
    PIECE("struct", M1_BOUNDARY_IDENT),   PIECE("return", M1_BOUNDARY_IDENT),
    PIECE("static", M1_BOUNDARY_IDENT),   PIECE("const", M1_BOUNDARY_IDENT),
    PIECE("sizeof", M1_BOUNDARY_IDENT),   PIECE("switch", M1_BOUNDARY_IDENT),
    PIECE("default", M1_BOUNDARY_IDENT),  PIECE("break", M1_BOUNDARY_IDENT),
    PIECE("continue", M1_BOUNDARY_IDENT), PIECE("while", M1_BOUNDARY_IDENT),
    PIECE("for", M1_BOUNDARY_IDENT),      PIECE("else", M1_BOUNDARY_IDENT),
    PIECE("enum", M1_BOUNDARY_IDENT),     PIECE("union", M1_BOUNDARY_IDENT),
    PIECE("inline", M1_BOUNDARY_IDENT),   PIECE("unsigned", M1_BOUNDARY_IDENT),
    PIECE("signed", M1_BOUNDARY_IDENT),   PIECE("void", M1_BOUNDARY_IDENT),
    PIECE("char", M1_BOUNDARY_IDENT),     PIECE("int", M1_BOUNDARY_IDENT),
    PIECE("long", M1_BOUNDARY_IDENT),     PIECE("float", M1_BOUNDARY_IDENT),
    PIECE("double", M1_BOUNDARY_IDENT),   PIECE("size_t", M1_BOUNDARY_IDENT),
    PIECE("uint8_t", M1_BOUNDARY_IDENT),  PIECE("uint16_t", M1_BOUNDARY_IDENT),
    PIECE("uint32_t", M1_BOUNDARY_IDENT), PIECE("uint64_t", M1_BOUNDARY_IDENT),
    PIECE("int32_t", M1_BOUNDARY_IDENT),  PIECE("int64_t", M1_BOUNDARY_IDENT),
    PIECE("bool", M1_BOUNDARY_IDENT),     PIECE("true", M1_BOUNDARY_IDENT),
    PIECE("false", M1_BOUNDARY_IDENT),    PIECE("NULL", M1_BOUNDARY_IDENT),
    PIECE("function", M1_BOUNDARY_IDENT), PIECE("import", M1_BOUNDARY_IDENT),
    PIECE("export", M1_BOUNDARY_IDENT),   PIECE("from", M1_BOUNDARY_IDENT),
    PIECE("class", M1_BOUNDARY_IDENT),    PIECE("interface", M1_BOUNDARY_IDENT),
    PIECE("async", M1_BOUNDARY_IDENT),    PIECE("await", M1_BOUNDARY_IDENT),
    PIECE("def", M1_BOUNDARY_IDENT),      PIECE("lambda", M1_BOUNDARY_IDENT),
    PIECE("None", M1_BOUNDARY_IDENT),     PIECE("True", M1_BOUNDARY_IDENT),
    PIECE("False", M1_BOUNDARY_IDENT),    PIECE("fn", M1_BOUNDARY_IDENT),
    PIECE("let", M1_BOUNDARY_IDENT),
};

static const M1FixedPiece k_landmark_pieces[M1_LANDMARK_COUNT] = {
    PIECE("diff --git ", M1_BOUNDARY_NONE),
    PIECE("index ", M1_BOUNDARY_HEADER),
    PIECE("--- a/", M1_BOUNDARY_NONE),
    PIECE("+++ b/", M1_BOUNDARY_NONE),
    PIECE("@@ -", M1_BOUNDARY_NONE),
    PIECE(" @@", M1_BOUNDARY_NONE),
    PIECE("error:", M1_BOUNDARY_HEADER),
    PIECE("warning:", M1_BOUNDARY_HEADER),
    PIECE("note:", M1_BOUNDARY_HEADER),
    PIECE("undefined reference to", M1_BOUNDARY_HEADER),
    PIECE("Traceback (most recent call last):", M1_BOUNDARY_HEADER),
    PIECE("AssertionError", M1_BOUNDARY_IDENT),
    PIECE("FAILED", M1_BOUNDARY_IDENT),
    PIECE("PASSED", M1_BOUNDARY_IDENT),
    PIECE("SELECT", M1_BOUNDARY_IDENT),
    PIECE("FROM", M1_BOUNDARY_IDENT),
    PIECE("WHERE", M1_BOUNDARY_IDENT),
    PIECE("GROUP BY", M1_BOUNDARY_IDENT),
    PIECE("ORDER BY", M1_BOUNDARY_IDENT),
    PIECE("INSERT INTO", M1_BOUNDARY_IDENT),
    PIECE("VALUES", M1_BOUNDARY_IDENT),
    PIECE("", M1_BOUNDARY_NONE),
};

static const char *const k_control_names[M1_CONTROL_COUNT] = {
    "<|bos|>",
    "<|eos|>",
    "<|pad|>",
    "<|system|>",
    "<|user|>",
    "<|assistant|>",
    "<|thought|>",
    "<|final|>",
    "<|tool_call|>",
    "<|tool_result|>",
    "<|fim_pre|>",
    "<|fim_suf|>",
};

static int is_ident_byte(uint8_t byte) {
  return isalnum(byte) || byte == '_';
}

static int boundary_allows(const uint8_t *data, size_t len, size_t pos, const M1FixedPiece *piece) {
  size_t end = pos + piece->len;
  if(piece->len == 0 || end > len) return 0;
  if(memcmp(data + pos, piece->text, piece->len) != 0) return 0;
  if(piece->boundary == M1_BOUNDARY_IDENT) {
    if(pos > 0 && is_ident_byte(data[pos - 1])) return 0;
    if(end < len && is_ident_byte(data[end])) return 0;
  } else if(piece->boundary == M1_BOUNDARY_HEADER) {
    if(pos > 0 && data[pos - 1] != '\n' && data[pos - 1] != '\r' && data[pos - 1] != ' ' &&
       data[pos - 1] != '\t' && data[pos - 1] != ':') {
      return 0;
    }
  }
  return 1;
}

static int match_fixed_piece(const uint8_t *data,
                             size_t len,
                             size_t pos,
                             uint32_t *out_token,
                             size_t *out_len) {
  uint32_t best_token = 0;
  size_t best_len = 0;

  if(data[pos] == '\n' || data[pos] == '\r') {
    for(uint32_t i = 0; i < M1_INDENT_COUNT; ++i) {
      const M1FixedPiece *piece = &k_indent_pieces[i];
      if(piece->len > best_len && boundary_allows(data, len, pos, piece)) {
        best_token = M1_INDENT_BASE + i;
        best_len = piece->len;
      }
    }
    if(best_len > 0) {
      *out_token = best_token;
      *out_len = best_len;
      return 1;
    }
  }

  for(uint32_t i = 0; i < M1_LANDMARK_COUNT - 1u; ++i) {
    const M1FixedPiece *piece = &k_landmark_pieces[i];
    if(piece->len > best_len && boundary_allows(data, len, pos, piece)) {
      best_token = M1_LANDMARK_BASE + i;
      best_len = piece->len;
    }
  }

  for(uint32_t i = 0; i < M1_PRIMITIVE_COUNT; ++i) {
    const M1FixedPiece *piece = &k_primitive_pieces[i];
    if(piece->len > best_len && boundary_allows(data, len, pos, piece)) {
      best_token = M1_PRIMITIVE_BASE + i;
      best_len = piece->len;
    }
  }

  if(best_len > 0) {
    *out_token = best_token;
    *out_len = best_len;
    return 1;
  }
  return 0;
}

static const M1FixedPiece *fixed_piece_for_token(uint32_t token) {
  if(token >= M1_INDENT_BASE && token < M1_INDENT_BASE + M1_INDENT_COUNT) {
    return &k_indent_pieces[token - M1_INDENT_BASE];
  }
  if(token >= M1_PRIMITIVE_BASE && token < M1_PRIMITIVE_BASE + M1_PRIMITIVE_COUNT) {
    return &k_primitive_pieces[token - M1_PRIMITIVE_BASE];
  }
  if(token >= M1_LANDMARK_BASE && token < M1_LANDMARK_BASE + M1_LANDMARK_COUNT) {
    return &k_landmark_pieces[token - M1_LANDMARK_BASE];
  }
  return NULL;
}

static int push_token_vec(uint32_t **items, size_t *count, size_t *cap, uint32_t token) {
  if(*count == *cap) {
    size_t next = *cap ? (*cap * 2u) : 64u;
    uint32_t *grown = realloc(*items, next * sizeof(uint32_t));
    if(!grown) return -1;
    *items = grown;
    *cap = next;
  }
  (*items)[(*count)++] = token;
  return 0;
}

static int append_bpe_span(const M1Tokenizer *tokenizer,
                           const uint8_t *span,
                           size_t span_len,
                           uint32_t **items,
                           size_t *count,
                           size_t *cap) {
  if(span_len == 0) return 0;
  if(!token_valid(&tokenizer->bpe)) {
    for(size_t i = 0; i < span_len; ++i) {
      if(push_token_vec(items, count, cap, span[i]) != 0) return -1;
    }
    return 0;
  }
  uint32_t *encoded = NULL;
  size_t encoded_count = 0;
  char err[256];
  if(!token_encode(&tokenizer->bpe, span, span_len, &encoded, &encoded_count, err)) return -1;
  for(size_t i = 0; i < encoded_count; ++i) {
    uint32_t mapped = encoded[i] < 256u
                          ? encoded[i]
                          : (M1_FIXED_VOCAB_SIZE + (encoded[i] - 256u));
    if(push_token_vec(items, count, cap, mapped) != 0) {
      free(encoded);
      return -1;
    }
  }
  free(encoded);
  return 0;
}

static int build_bpe_training_buffer(const uint8_t *data, size_t len, Buffer *out) {
  buffer_init(out);
  if(len == 0) return 0;

  const size_t segments = len >= 4096u ? 10u : 1u;
  const size_t seg_len = len / segments;
  for(size_t seg = 0; seg < segments; ++seg) {
    size_t seg_start = seg * seg_len;
    size_t seg_end = (seg + 1u == segments) ? len : (seg_start + seg_len);
    size_t train_end = seg_start + ((seg_end - seg_start) * 7u) / 10u;
    if(train_end <= seg_start) train_end = seg_end;

    size_t pos = seg_start;
    while(pos < train_end) {
      uint32_t token = 0;
      size_t piece_len = 0;
      if(match_fixed_piece(data, train_end, pos, &token, &piece_len)) {
        (void)token;
        pos += piece_len;
      } else {
        buffer_append(out, data + pos, 1u);
        pos++;
      }
    }
  }
  return 0;
}

void m1_tokenizer_init(M1Tokenizer *tokenizer) {
  memset(tokenizer, 0, sizeof(*tokenizer));
  tokenizer->target_vocab_size = M1_FIXED_VOCAB_SIZE;
  sha256_hex("oyez.m1.tokenizer.empty", 23u, tokenizer->sha256);
}

void m1_tokenizer_free(M1Tokenizer *tokenizer) {
  if(!tokenizer) return;
  token_free(&tokenizer->bpe);
  memset(tokenizer, 0, sizeof(*tokenizer));
}

int m1_tokenizer_train(M1Tokenizer *tokenizer, const uint8_t *data, size_t len, uint32_t vocab_size) {
  if(!tokenizer || (!data && len > 0)) return -1;
  m1_tokenizer_free(tokenizer);
  m1_tokenizer_init(tokenizer);

  if(vocab_size < M1_FIXED_VOCAB_SIZE) vocab_size = M1_FIXED_VOCAB_SIZE;
  if(vocab_size > M1_DEFAULT_VOCAB_SIZE) vocab_size = M1_DEFAULT_VOCAB_SIZE;
  tokenizer->target_vocab_size = vocab_size;

  uint32_t bpe_merges = vocab_size - M1_FIXED_VOCAB_SIZE;
  Buffer filtered;
  if(build_bpe_training_buffer(data, len, &filtered) != 0) return -1;

  TokenTrainConfig cfg = token_train_config_default();
  cfg.target_vocab = 256u + bpe_merges;
  cfg.min_frequency = 2u;
  char err[256];
  bool ok = token_train(&tokenizer->bpe,
                        filtered.data ? filtered.data : "",
                        filtered.len,
                        NULL,
                        0u,
                        NULL,
                        0u,
                        &cfg,
                        err);
  buffer_free(&filtered);
  return ok ? 0 : -1;
}

uint32_t m1_tokenizer_vocab_size(const M1Tokenizer *tokenizer) {
  if(!tokenizer) return 0;
  uint32_t learned = tokenizer->bpe.vocab > 256u ? (tokenizer->bpe.vocab - 256u) : 0u;
  return M1_FIXED_VOCAB_SIZE + learned;
}

const char *m1_tokenizer_control_name(uint32_t token) {
  if(token == M1_CTRL_FIM_MID) return "<|fim_mid|>";
  if(token >= M1_CONTROL_BASE && token < M1_CONTROL_BASE + M1_CONTROL_COUNT) {
    return k_control_names[token - M1_CONTROL_BASE];
  }
  return NULL;
}

int m1_tokenizer_encode(const M1Tokenizer *tokenizer,
                        const uint8_t *data,
                        size_t len,
                        uint32_t **out_tokens,
                        size_t *out_count) {
  if(!tokenizer || !out_tokens || !out_count || (!data && len > 0)) return -1;
  *out_tokens = NULL;
  *out_count = 0;
  if(len == 0) return 0;

  uint32_t *items = NULL;
  size_t count = 0;
  size_t cap = 0;
  size_t span_start = 0;
  size_t pos = 0;

  while(pos < len) {
    uint32_t fixed_token = 0;
    size_t piece_len = 0;
    if(match_fixed_piece(data, len, pos, &fixed_token, &piece_len)) {
      if(pos > span_start) {
        if(append_bpe_span(tokenizer, data + span_start, pos - span_start, &items, &count, &cap) != 0) {
          free(items);
          return -1;
        }
      }
      if(push_token_vec(&items, &count, &cap, fixed_token) != 0) {
        free(items);
        return -1;
      }
      pos += piece_len;
      span_start = pos;
    } else {
      pos++;
    }
  }

  if(span_start < len) {
    if(append_bpe_span(tokenizer, data + span_start, len - span_start, &items, &count, &cap) != 0) {
      free(items);
      return -1;
    }
  }

  *out_tokens = items;
  *out_count = count;
  return 0;
}

int m1_tokenizer_decode(const M1Tokenizer *tokenizer,
                        const uint32_t *tokens,
                        size_t count,
                        uint8_t **out_bytes,
                        size_t *out_len) {
  if(!tokenizer || !out_bytes || !out_len || (!tokens && count > 0)) return -1;
  Buffer buf;
  buffer_init(&buf);

  uint32_t vocab_size = m1_tokenizer_vocab_size(tokenizer);
  for(size_t i = 0; i < count; ++i) {
    uint32_t token = tokens[i];
    if(token >= vocab_size) {
      buffer_free(&buf);
      return -1;
    }
    if(token < 256u) {
      char byte = (char)token;
      buffer_append(&buf, &byte, 1u);
      continue;
    }

    const M1FixedPiece *piece = fixed_piece_for_token(token);
    if(piece && token != M1_CTRL_FIM_MID) {
      buffer_append(&buf, piece->text, piece->len);
      continue;
    }

    if(token == M1_CTRL_FIM_MID || (token >= M1_CONTROL_BASE && token < M1_FIXED_VOCAB_SIZE)) {
      continue;
    }

    uint32_t bpe_token = 256u + (token - M1_FIXED_VOCAB_SIZE);
    if(bpe_token >= tokenizer->bpe.vocab) {
      buffer_free(&buf);
      return -1;
    }
    char err[256];
    if(!token_decode(&tokenizer->bpe, &bpe_token, 1u, &buf, err)) {
      buffer_free(&buf);
      return -1;
    }
  }

  uint8_t *bytes = malloc(buf.len ? buf.len : 1u);
  if(!bytes) {
    buffer_free(&buf);
    return -1;
  }
  if(buf.len > 0) memcpy(bytes, buf.data, buf.len);
  *out_bytes = bytes;
  *out_len = buf.len;
  buffer_free(&buf);
  return 0;
}

int m1_tokenizer_save(M1Tokenizer *tokenizer, const char *path) {
  if(!tokenizer || !path) return -1;
  Buffer bpe_buf;
  buffer_init(&bpe_buf);
  if(token_valid(&tokenizer->bpe)) {
    if(!token_save(&tokenizer->bpe, &bpe_buf)) {
      buffer_free(&bpe_buf);
      return -1;
    }
  }

  Buffer payload;
  buffer_init(&payload);
  buffer_append(&payload, "OYEZMT02", 8u);
  wire_append_u32(&payload, tokenizer->target_vocab_size);
  wire_append_u32(&payload, (uint32_t)bpe_buf.len);
  if(bpe_buf.len > 0) buffer_append(&payload, bpe_buf.data, bpe_buf.len);
  buffer_free(&bpe_buf);

  char digest[65];
  sha256_hex(payload.data, payload.len, digest);
  uint32_t crc = checksum_crc32(payload.data, payload.len);
  buffer_append(&payload, digest, 64u);
  wire_append_u32(&payload, crc);

  FILE *fp = fopen(path, "wb");
  if(!fp) {
    buffer_free(&payload);
    return -1;
  }
  size_t written = fwrite(payload.data, 1u, payload.len, fp);
  fclose(fp);
  size_t expected = payload.len;
  buffer_free(&payload);
  if(written != expected) return -1;
  memcpy(tokenizer->sha256, digest, 65u);
  return 0;
}

int m1_tokenizer_load(M1Tokenizer *tokenizer, const char *path) {
  if(!tokenizer || !path) return -1;
  FILE *fp = fopen(path, "rb");
  if(!fp) return -1;
  if(fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if(size < 84 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }

  unsigned char *bytes = malloc((size_t)size);
  if(!bytes) {
    fclose(fp);
    return -1;
  }
  if(fread(bytes, 1u, (size_t)size, fp) != (size_t)size) {
    free(bytes);
    fclose(fp);
    return -1;
  }
  fclose(fp);

  size_t payload_len = (size_t)size - 68u;
  char stored_sha[65];
  memcpy(stored_sha, bytes + payload_len, 64u);
  stored_sha[64] = '\0';
  size_t crc_off = payload_len + 64u;
  uint32_t stored_crc = 0;
  wire_read_u32(bytes, (size_t)size, &crc_off, &stored_crc);
  char actual_sha[65];
  sha256_hex(bytes, payload_len, actual_sha);
  if(memcmp(stored_sha, actual_sha, 64u) != 0 ||
     stored_crc != checksum_crc32(bytes, payload_len) ||
     memcmp(bytes, "OYEZMT02", 8u) != 0) {
    free(bytes);
    return -1;
  }

  size_t off = 8u;
  uint32_t target_vocab = 0;
  uint32_t bpe_len = 0;
  if(!wire_read_u32(bytes, payload_len, &off, &target_vocab) ||
     !wire_read_u32(bytes, payload_len, &off, &bpe_len) ||
     off + bpe_len != payload_len) {
    free(bytes);
    return -1;
  }

  m1_tokenizer_free(tokenizer);
  m1_tokenizer_init(tokenizer);
  tokenizer->target_vocab_size = target_vocab;
  if(bpe_len > 0) {
    char err[256];
    if(!token_load(&tokenizer->bpe, bytes + off, bpe_len, err)) {
      free(bytes);
      return -1;
    }
  }
  memcpy(tokenizer->sha256, actual_sha, 65u);
  free(bytes);
  return 0;
}

void m1_sequence_init(M1Sequence *seq) {
  memset(seq, 0, sizeof(*seq));
}

void m1_sequence_free(M1Sequence *seq) {
  if(!seq) return;
  free(seq->tokens);
  free(seq->loss_mask);
  memset(seq, 0, sizeof(*seq));
}

int m1_sequence_push_token(M1Sequence *seq, uint32_t token, uint8_t loss_mask) {
  if(!seq) return -1;
  if(seq->length == seq->capacity) {
    size_t next = seq->capacity ? (seq->capacity * 2u) : 64u;
    uint32_t *tokens = realloc(seq->tokens, next * sizeof(uint32_t));
    if(!tokens) return -1;
    seq->tokens = tokens;
    uint8_t *masks = realloc(seq->loss_mask, next * sizeof(uint8_t));
    if(!masks) return -1;
    seq->loss_mask = masks;
    seq->capacity = next;
  }
  seq->tokens[seq->length] = token;
  seq->loss_mask[seq->length] = loss_mask ? 1u : 0u;
  seq->length++;
  return 0;
}

int m1_sequence_append_text(M1Sequence *seq,
                            const M1Tokenizer *tokenizer,
                            const char *text,
                            size_t len,
                            uint8_t loss_mask) {
  if(!seq || !tokenizer || (!text && len > 0)) return -1;
  uint32_t *tokens = NULL;
  size_t count = 0;
  if(m1_tokenizer_encode(tokenizer, (const uint8_t *)text, len, &tokens, &count) != 0) return -1;
  for(size_t i = 0; i < count; ++i) {
    if(m1_sequence_push_token(seq, tokens[i], loss_mask) != 0) {
      free(tokens);
      return -1;
    }
  }
  free(tokens);
  return 0;
}

int m1_sequence_begin(M1Sequence *seq) {
  return m1_sequence_push_token(seq, M1_CTRL_BOS, 0u);
}

int m1_sequence_end(M1Sequence *seq) {
  return m1_sequence_push_token(seq, M1_CTRL_EOS, 1u);
}

int m1_sequence_message(M1Sequence *seq,
                        const M1Tokenizer *tokenizer,
                        M1ControlToken role_token,
                        const char *text,
                        size_t len,
                        uint8_t loss_mask) {
  if(m1_sequence_push_token(seq, (uint32_t)role_token, 0u) != 0) return -1;
  return m1_sequence_append_text(seq, tokenizer, text, len, loss_mask);
}

int m1_sequence_assistant(M1Sequence *seq,
                          const M1Tokenizer *tokenizer,
                          const char *thought,
                          size_t thought_len,
                          const char *final_text,
                          size_t final_len,
                          uint8_t include_thought_loss) {
  if(m1_sequence_push_token(seq, M1_CTRL_ASSISTANT, 0u) != 0) return -1;
  if(thought && thought_len > 0) {
    if(m1_sequence_push_token(seq, M1_CTRL_THOUGHT, 0u) != 0) return -1;
    if(m1_sequence_append_text(seq,
                               tokenizer,
                               thought,
                               thought_len,
                               include_thought_loss ? 1u : 0u) != 0) {
      return -1;
    }
  }
  if(m1_sequence_push_token(seq, M1_CTRL_FINAL, 0u) != 0) return -1;
  return m1_sequence_append_text(seq, tokenizer, final_text, final_len, 1u);
}
