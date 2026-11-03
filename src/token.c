#include "token.h"
#include "checksum.h"
#include "wire.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NIL 0xFFFFFFFFu

static inline uint64_t mkkey(uint32_t l, uint32_t r) { return ((uint64_t)l << 32) | r; }
#define KEY_L(k) ((uint32_t)((k) >> 32))
#define KEY_R(k) ((uint32_t)((k) & 0xFFFFFFFFu))

typedef struct { uint32_t id, prev, next; bool alive; } Sym;

typedef struct {
    uint64_t key;
    int64_t count;
    uint32_t *pos;
    uint32_t npos, cappos;
    bool used;
} PairSlot;

typedef struct { PairSlot *slots; size_t cap, mask, used_count; } PairTable;

static bool pt_init(PairTable *pt, size_t hint)
{
    size_t cap = 16;
    while (cap < hint * 2) cap <<= 1;
    pt->slots = calloc(cap, sizeof(PairSlot));
    if (!pt->slots) return false;
    pt->cap = cap; pt->mask = cap - 1; pt->used_count = 0;
    return true;
}

static void pt_free(PairTable *pt)
{
    if (pt->slots) {
        for (size_t i = 0; i < pt->cap; ++i) free(pt->slots[i].pos);
        free(pt->slots);
    }
    *pt = (PairTable){0};
}

static PairSlot *pt_find(PairTable *pt, uint64_t key)
{
    size_t i = mix64(key) & pt->mask;
    for (;;) {
        PairSlot *s = &pt->slots[i];
        if (!s->used) return NULL;
        if (s->key == key) return s;
        i = (i + 1) & pt->mask;
    }
}

static bool pt_grow(PairTable *pt)
{
    size_t ncap = pt->cap << 1;
    PairSlot *ns = calloc(ncap, sizeof(PairSlot));
    if (!ns) return false;
    size_t nmask = ncap - 1;
    for (size_t i = 0; i < pt->cap; ++i) {
        PairSlot *o = &pt->slots[i];
        if (!o->used) continue;
        size_t j = mix64(o->key) & nmask;
        while (ns[j].used) j = (j + 1) & nmask;
        ns[j] = *o;
    }
    free(pt->slots);
    pt->slots = ns; pt->cap = ncap; pt->mask = nmask;
    return true;
}

static PairSlot *pt_add(PairTable *pt, uint64_t key)
{
    if ((pt->used_count + 1) * 10 >= pt->cap * 7 && !pt_grow(pt)) return NULL;
    size_t i = mix64(key) & pt->mask;
    for (;;) {
        PairSlot *s = &pt->slots[i];
        if (!s->used) {
            s->used = true; s->key = key; s->count = 0; s->pos = NULL; s->npos = s->cappos = 0;
            pt->used_count++;
            return s;
        }
        if (s->key == key) return s;
        i = (i + 1) & pt->mask;
    }
}

static bool pos_push(PairSlot *s, uint32_t p)
{
    if (s->npos == s->cappos) {
        uint32_t nc = s->cappos ? s->cappos * 2 : 4;
        uint32_t *np = realloc(s->pos, nc * sizeof(uint32_t));
        if (!np) return false;
        s->pos = np; s->cappos = nc;
    }
    s->pos[s->npos++] = p;
    return true;
}

typedef struct { int64_t count; uint64_t key; } HEnt;
typedef struct { HEnt *a; size_t n, cap; } MaxHeap;

static bool h_gt(const HEnt *x, const HEnt *y)
{
    if (x->count != y->count) return x->count > y->count;
    return x->key < y->key;
}

static bool mh_push(MaxHeap *h, int64_t count, uint64_t key)
{
    if (h->n == h->cap) {
        size_t nc = h->cap ? h->cap * 2 : 64;
        HEnt *na = realloc(h->a, nc * sizeof(HEnt));
        if (!na) return false;
        h->a = na; h->cap = nc;
    }
    size_t i = h->n++;
    h->a[i] = (HEnt){count, key};
    while (i) {
        size_t p = (i - 1) / 2;
        if (!h_gt(&h->a[i], &h->a[p])) break;
        HEnt t = h->a[i]; h->a[i] = h->a[p]; h->a[p] = t; i = p;
    }
    return true;
}

static bool mh_pop(MaxHeap *h, HEnt *out)
{
    if (!h->n) return false;
    *out = h->a[0];
    h->a[0] = h->a[--h->n];
    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, b = i;
        if (l < h->n && h_gt(&h->a[l], &h->a[b])) b = l;
        if (r < h->n && h_gt(&h->a[r], &h->a[b])) b = r;
        if (b == i) break;
        HEnt t = h->a[i]; h->a[i] = h->a[b]; h->a[b] = t; i = b;
    }
    return true;
}

typedef struct { uint64_t *keys; uint32_t *vals; bool *used; size_t cap, mask; } RankMap;

static RankMap *rank_build(const uint32_t *ml, const uint32_t *mr, uint32_t n)
{
    RankMap *rm = calloc(1, sizeof(RankMap));
    if (!rm) return NULL;
    size_t cap = 16;
    while (cap < (size_t)n * 2) cap <<= 1;
    rm->keys = calloc(cap, sizeof(uint64_t));
    rm->vals = calloc(cap, sizeof(uint32_t));
    rm->used = calloc(cap, sizeof(bool));
    if (!rm->keys || !rm->vals || !rm->used) {
        free(rm->keys); free(rm->vals); free(rm->used); free(rm); return NULL;
    }
    rm->cap = cap; rm->mask = cap - 1;
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t k = mkkey(ml[i], mr[i]);
        size_t j = mix64(k) & rm->mask;
        while (rm->used[j]) {
            if (rm->keys[j] == k) {
                free(rm->keys); free(rm->vals); free(rm->used); free(rm); return NULL;
            }
            j = (j + 1) & rm->mask;
        }
        rm->used[j] = true; rm->keys[j] = k; rm->vals[j] = i;
    }
    return rm;
}

static void rank_free(RankMap *rm)
{
    if (!rm) return;
    free(rm->keys); free(rm->vals); free(rm->used); free(rm);
}

static bool rank_get(const RankMap *rm, uint32_t l, uint32_t r, uint32_t *rank)
{
    uint64_t k = mkkey(l, r);
    size_t j = mix64(k) & rm->mask;
    for (;;) {
        if (!rm->used[j]) return false;
        if (rm->keys[j] == k) { *rank = rm->vals[j]; return true; }
        j = (j + 1) & rm->mask;
    }
}

static bool build_pieces(Tokenizer *t)
{
    const size_t n_tok = TOKEN_BASE + t->n_merges;
    size_t *off = malloc((n_tok + 1) * sizeof(size_t));
    if (!off) return false;
    size_t *len = malloc(n_tok * sizeof(size_t));
    if (!len) { free(off); return false; }
    for (size_t i = 0; i < TOKEN_BASE; ++i) len[i] = 1;
    for (uint32_t i = 0; i < t->n_merges; ++i) {
        uint32_t L = t->merge_left[i], R = t->merge_right[i];
        len[TOKEN_BASE + i] = len[L] + len[R];
    }
    off[0] = 0;
    for (size_t i = 0; i < n_tok; ++i) off[i + 1] = off[i] + len[i];
    const size_t total = off[n_tok];
    unsigned char *pool = malloc(total ? total : 1);
    if (!pool) { free(off); free(len); return false; }
    for (size_t b = 0; b < TOKEN_BASE; ++b) pool[off[b]] = (unsigned char)b;
    for (uint32_t i = 0; i < t->n_merges; ++i) {
        uint32_t L = t->merge_left[i], R = t->merge_right[i];
        unsigned char *dst = pool + off[TOKEN_BASE + i];
        memcpy(dst, pool + off[L], len[L]);
        memcpy(dst + len[L], pool + off[R], len[R]);
    }
    free(len);
    t->piece_pool = pool;
    t->piece_off = off;
    return true;
}

TokenTrainConfig token_train_config_default(void)
{
    return (TokenTrainConfig){ .target_vocab = 1024, .min_frequency = 2 };
}

void token_free(Tokenizer *t)
{
    if (!t) return;
    free(t->merge_left);
    free(t->merge_right);
    if (t->special) for (uint32_t i = 0; i < t->n_special; ++i) free(t->special[i]);
    free(t->special);
    free(t->piece_pool);
    free(t->piece_off);
    rank_free((RankMap *)t->rank_index);
    *t = (Tokenizer){0};
}

bool token_valid(const Tokenizer *t)
{
    if (!t) return false;
    if (t->vocab != TOKEN_BASE + t->n_merges + t->n_special) return false;
    if (t->vocab > TOKEN_MAX_VOCAB) return false;
    if (t->n_merges && (!t->merge_left || !t->merge_right)) return false;
    if (t->n_special && !t->special) return false;
    if (!t->piece_off || !t->piece_pool || !t->rank_index) return false;
    for (uint32_t i = 0; i < t->n_merges; ++i)
        if (t->merge_left[i] >= TOKEN_BASE + i || t->merge_right[i] >= TOKEN_BASE + i) return false;
    return true;
}

uint32_t token_vocab(const Tokenizer *t) { return t ? t->vocab : 0; }

static bool finalize(Tokenizer *t, char error[256])
{
    if (!build_pieces(t)) { if (error) snprintf(error, 256, "token: piece table allocation failed"); return false; }
    RankMap *rm = rank_build(t->merge_left, t->merge_right, t->n_merges);
    if (!rm) { if (error) snprintf(error, 256, "token: rank index build failed (duplicate pair or OOM)"); return false; }
    t->rank_index = rm;
    t->vocab = TOKEN_BASE + t->n_merges + t->n_special;
    return true;
}

bool token_train(Tokenizer *t, const void *data, size_t length,
                 const uint32_t *seg_offsets, size_t seg_count,
                 const char *const *special, size_t n_special,
                 const TokenTrainConfig *cfg, char error[256])
{
    if (error) error[0] = 0;
    if (!t || (!data && length)) { if (error) snprintf(error, 256, "token: null arguments"); return false; }
    if (length > TOKEN_MAX_CORPUS) { if (error) snprintf(error, 256, "token: corpus too large"); return false; }
    if (n_special > TOKEN_MAX_SPECIAL) { if (error) snprintf(error, 256, "token: too many special tokens"); return false; }
    *t = (Tokenizer){0};

    TokenTrainConfig c = cfg ? *cfg : token_train_config_default();
    if (c.min_frequency < 2) c.min_frequency = 2;
    uint32_t target = c.target_vocab;
    if (target > TOKEN_MAX_VOCAB) target = TOKEN_MAX_VOCAB;
    uint32_t base_plus_special = TOKEN_BASE + (uint32_t)n_special;
    uint32_t max_merges = (target > base_plus_special) ? target - base_plus_special : 0;

    const unsigned char *bytes = data;
    bool ok = false;
    Sym *sym = NULL;
    PairTable pt = {0};
    MaxHeap heap = {0};
    uint32_t *posbuf = NULL;
    size_t posbuf_cap = 0;

    if (n_special) {
        t->special = calloc(n_special, sizeof(char *));
        if (!t->special) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        for (size_t i = 0; i < n_special; ++i) {
            const char *s = special ? special[i] : NULL;
            if (!s) { if (error) snprintf(error, 256, "token: null special name"); goto done; }
            size_t n = strlen(s);
            t->special[i] = malloc(n + 1);
            if (!t->special[i]) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
            memcpy(t->special[i], s, n + 1);
            t->n_special++;
        }
    }

    if (max_merges) {
        t->merge_left = malloc(max_merges * sizeof(uint32_t));
        t->merge_right = malloc(max_merges * sizeof(uint32_t));
        if (!t->merge_left || !t->merge_right) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
    }

    if (length) {
        sym = malloc(length * sizeof(Sym));
        if (!sym) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        for (size_t i = 0; i < length; ++i) {
            sym[i].id = bytes[i];
            sym[i].prev = (i == 0) ? NIL : (uint32_t)(i - 1);
            sym[i].next = (i + 1 < length) ? (uint32_t)(i + 1) : NIL;
            sym[i].alive = true;
        }
        if (seg_offsets && seg_count) {
            if (seg_offsets[0] != 0 || seg_offsets[seg_count] != length) {
                if (error) snprintf(error, 256, "token: segment offsets must span [0,length]");
                goto done;
            }
            for (size_t s = 1; s < seg_count; ++s) {
                uint32_t b = seg_offsets[s];
                if (b == 0 || b >= length || seg_offsets[s] < seg_offsets[s - 1]) {
                    if (error) snprintf(error, 256, "token: bad segment offset");
                    goto done;
                }
                sym[b - 1].next = NIL;
                sym[b].prev = NIL;
            }
        }
    }

    if (max_merges == 0 || length < 2) { ok = true; goto finish; }

    if (!pt_init(&pt, length)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }

    for (uint32_t i = 0; i < length; ++i) {
        if (!sym[i].alive || sym[i].next == NIL) continue;
        uint64_t k = mkkey(sym[i].id, sym[sym[i].next].id);
        PairSlot *s = pt_add(&pt, k);
        if (!s) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        s->count++;
        if (!pos_push(s, i)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
    }
    for (size_t i = 0; i < pt.cap; ++i)
        if (pt.slots[i].used && pt.slots[i].count > 0)
            if (!mh_push(&heap, pt.slots[i].count, pt.slots[i].key)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }

    while (t->n_merges < max_merges) {
        HEnt top;
        PairSlot *best = NULL;
        for (;;) {
            if (!mh_pop(&heap, &top)) break;
            PairSlot *s = pt_find(&pt, top.key);
            if (!s || s->count <= 0) continue;
            if (s->count != top.count) {
                if (!mh_push(&heap, s->count, top.key)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
                continue;
            }
            best = s; break;
        }
        if (!best || best->count < (int64_t)c.min_frequency) break;

        uint64_t pkey = best->key;
        uint32_t A = KEY_L(pkey), B = KEY_R(pkey);
        uint32_t Z = TOKEN_BASE + t->n_merges;
        t->merge_left[t->n_merges] = A;
        t->merge_right[t->n_merges] = B;
        t->n_merges++;

        uint32_t np = best->npos;
        if (np > posbuf_cap) {
            uint32_t *nb = realloc(posbuf, np * sizeof(uint32_t));
            if (!nb) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
            posbuf = nb; posbuf_cap = np;
        }
        memcpy(posbuf, best->pos, np * sizeof(uint32_t));

        for (uint32_t idx = 0; idx < np; ++idx) {
            uint32_t p = posbuf[idx];
            if (!sym[p].alive || sym[p].id != A) continue;
            uint32_t q = sym[p].next;
            if (q == NIL || !sym[q].alive || sym[q].id != B) continue;

            uint32_t pprev = sym[p].prev, qnext = sym[q].next;
            if (pprev != NIL && sym[pprev].alive) {
                PairSlot *d = pt_find(&pt, mkkey(sym[pprev].id, A));
                if (d && d->count > 0) d->count--;
            }
            if (qnext != NIL && sym[qnext].alive) {
                PairSlot *d = pt_find(&pt, mkkey(B, sym[qnext].id));
                if (d && d->count > 0) d->count--;
            }
            sym[p].id = Z;
            sym[p].next = qnext;
            if (qnext != NIL) sym[qnext].prev = p;
            sym[q].alive = false;
            if (pprev != NIL && sym[pprev].alive) {
                uint64_t k = mkkey(sym[pprev].id, Z);
                PairSlot *s = pt_add(&pt, k);
                if (!s || !pos_push(s, pprev)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
                s->count++;
                if (!mh_push(&heap, s->count, k)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
            }
            if (qnext != NIL && sym[qnext].alive) {
                uint64_t k = mkkey(Z, sym[qnext].id);
                PairSlot *s = pt_add(&pt, k);
                if (!s || !pos_push(s, p)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
                s->count++;
                if (!mh_push(&heap, s->count, k)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
            }
        }
        PairSlot *cleared = pt_find(&pt, pkey);
        if (cleared) { cleared->count = 0; free(cleared->pos); cleared->pos = NULL; cleared->npos = cleared->cappos = 0; }
    }

finish:
    if (!finalize(t, error)) goto done;
    ok = true;

done:
    free(sym);
    pt_free(&pt);
    free(heap.a);
    free(posbuf);
    if (!ok) token_free(t);
    return ok;
}

typedef struct { uint32_t rank, pos; } REnt;
typedef struct { REnt *a; size_t n, cap; } MinHeap;

static bool re_lt(const REnt *x, const REnt *y)
{
    if (x->rank != y->rank) return x->rank < y->rank;
    return x->pos < y->pos;
}

static bool nh_push(MinHeap *h, uint32_t rank, uint32_t pos)
{
    if (h->n == h->cap) {
        size_t nc = h->cap ? h->cap * 2 : 64;
        REnt *na = realloc(h->a, nc * sizeof(REnt));
        if (!na) return false;
        h->a = na; h->cap = nc;
    }
    size_t i = h->n++;
    h->a[i] = (REnt){rank, pos};
    while (i) {
        size_t p = (i - 1) / 2;
        if (!re_lt(&h->a[i], &h->a[p])) break;
        REnt t = h->a[i]; h->a[i] = h->a[p]; h->a[p] = t; i = p;
    }
    return true;
}

static bool nh_pop(MinHeap *h, REnt *out)
{
    if (!h->n) return false;
    *out = h->a[0];
    h->a[0] = h->a[--h->n];
    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, b = i;
        if (l < h->n && re_lt(&h->a[l], &h->a[b])) b = l;
        if (r < h->n && re_lt(&h->a[r], &h->a[b])) b = r;
        if (b == i) break;
        REnt t = h->a[i]; h->a[i] = h->a[b]; h->a[b] = t; i = b;
    }
    return true;
}

static bool encode_symbols(const Tokenizer *t, const void *bytes, const uint32_t *symbols, size_t length,
                           uint32_t **out, size_t *n_out, char error[256])
{
    if (error) error[0] = 0;
    if (out) *out = NULL;
    if (n_out) *n_out = 0;
    if (!token_valid(t) || !out || !n_out || (!bytes && !symbols && length)) {
        if (error) snprintf(error, 256, "token: invalid encode arguments");
        return false;
    }
    if (length > TOKEN_MAX_CORPUS) { if (error) snprintf(error, 256, "token: input too large"); return false; }
    if (symbols) for (size_t i = 0; i < length; ++i) if (symbols[i] >= TOKEN_MAX_VOCAB) {
        if (error) snprintf(error, 256, "token: symbol outside vocabulary address space"); return false;
    }

    const unsigned char *b = bytes;
    if (length == 0) { *out = malloc(sizeof(**out)); if (!*out) return false; *n_out = 0; return true; }

    const RankMap *rm = (const RankMap *)t->rank_index;
    Sym *sym = malloc(length * sizeof(Sym));
    if (!sym) { if (error) snprintf(error, 256, "token: OOM"); return false; }
    for (size_t i = 0; i < length; ++i) {
        sym[i].id = symbols ? symbols[i] : b[i];
        sym[i].prev = (i == 0) ? NIL : (uint32_t)(i - 1);
        sym[i].next = (i + 1 < length) ? (uint32_t)(i + 1) : NIL;
        sym[i].alive = true;
    }

    bool ok = false;
    MinHeap heap = {0};
    for (uint32_t i = 0; i + 1 < length; ++i) {
        uint32_t rank;
        if (rank_get(rm, sym[i].id, sym[i + 1].id, &rank))
            if (!nh_push(&heap, rank, i)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
    }

    REnt e;
    while (nh_pop(&heap, &e)) {
        uint32_t p = e.pos;
        if (!sym[p].alive) continue;
        uint32_t q = sym[p].next;
        if (q == NIL || !sym[q].alive) continue;
        uint32_t rank;
        if (!rank_get(rm, sym[p].id, sym[q].id, &rank) || rank != e.rank) continue;

        uint32_t Z = TOKEN_BASE + e.rank;
        uint32_t pprev = sym[p].prev, qnext = sym[q].next;
        sym[p].id = Z;
        sym[p].next = qnext;
        if (qnext != NIL) sym[qnext].prev = p;
        sym[q].alive = false;

        if (pprev != NIL && sym[pprev].alive) {
            uint32_t nr;
            if (rank_get(rm, sym[pprev].id, Z, &nr))
                if (!nh_push(&heap, nr, pprev)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        }
        if (qnext != NIL && sym[qnext].alive) {
            uint32_t nr;
            if (rank_get(rm, Z, sym[qnext].id, &nr))
                if (!nh_push(&heap, nr, p)) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        }
    }

    {
        uint32_t *ids = malloc(length * sizeof(uint32_t));
        if (!ids) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        size_t n = 0;
        for (uint32_t cur = 0; cur != NIL; cur = sym[cur].next) {
            ids[n++] = sym[cur].id;
        }
        *out = ids;
        *n_out = n;
    }
    ok = true;

done:
    free(sym);
    free(heap.a);
    return ok;
}

bool token_encode(const Tokenizer *t, const void *bytes, size_t length, uint32_t **out, size_t *count, char error[256])
{
    return encode_symbols(t, bytes, NULL, length, out, count, error);
}

bool token_merge_ids(const Tokenizer *t, const uint32_t *symbols, size_t count, uint32_t **out, size_t *n_out, char error[256])
{
    return encode_symbols(t, NULL, symbols, count, out, n_out, error);
}

bool token_decode(const Tokenizer *t, const uint32_t *ids, size_t count, Buffer *out, char error[256])
{
    if (error) error[0] = 0;
    if (!token_valid(t) || (!ids && count) || !out) {
        if (error) snprintf(error, 256, "token: invalid decode arguments");
        return false;
    }
    const uint32_t byte_backed = TOKEN_BASE + t->n_merges;
    for (size_t i = 0; i < count; ++i) {
        uint32_t id = ids[i];
        if (id >= t->vocab) { if (error) snprintf(error, 256, "token: id %u out of range", id); return false; }
        if (id < byte_backed) {
            size_t a = t->piece_off[id], len = t->piece_off[id + 1] - a;
            buffer_append(out, t->piece_pool + a, len);
        }
    }
    return !out->truncated;
}

bool token_special_id(const Tokenizer *t, const char *name, uint32_t *id)
{
    if (!token_valid(t) || !name || !id) return false;
    for (uint32_t i = 0; i < t->n_special; ++i)
        if (strcmp(t->special[i], name) == 0) { *id = TOKEN_BASE + t->n_merges + i; return true; }
    return false;
}

const char *token_id_name(const Tokenizer *t, uint32_t id)
{
    if (!token_valid(t)) return NULL;
    uint32_t first = TOKEN_BASE + t->n_merges;
    if (id < first || id >= t->vocab) return NULL;
    return t->special[id - first];
}

bool token_save(const Tokenizer *t, Buffer *out)
{
    if (!token_valid(t) || !out) return false;
    Buffer body = {0};
    wire_append_u32(&body, 1);
    wire_append_u32(&body, t->n_merges);
    wire_append_u32(&body, t->n_special);
    for (uint32_t i = 0; i < t->n_merges; ++i) { wire_append_u32(&body, t->merge_left[i]); wire_append_u32(&body, t->merge_right[i]); }
    for (uint32_t i = 0; i < t->n_special; ++i) {
        size_t n = strlen(t->special[i]);
        wire_append_u32(&body, (uint32_t)n);
        buffer_append(&body, t->special[i], n);
    }
    if (body.truncated) { buffer_free(&body); return false; }
    const uint32_t crc = checksum_crc32(body.data, body.len);
    buffer_text(out, "OYEZBP01");
    buffer_append(out, body.data, body.len);
    wire_append_u32(out, crc);
    buffer_free(&body);
    return !out->truncated;
}

bool token_load(Tokenizer *t, const void *bytes, size_t length, char error[256])
{
    if (error) error[0] = 0;
    if (!t || !bytes || length < 8 + 4) { if (error) snprintf(error, 256, "token: short blob"); return false; }
    *t = (Tokenizer){0};
    const unsigned char *p = bytes;
    if (memcmp(p, "OYEZBP01", 8)) { if (error) snprintf(error, 256, "token: bad magic"); return false; }
    const unsigned char *body = p + 8;
    const size_t body_len = length - 8 - 4;
    size_t cpos = length - 4;
    uint32_t stored;
    if (!wire_read_u32(p, length, &cpos, &stored)) { if (error) snprintf(error, 256, "token: bad crc"); return false; }
    if (checksum_crc32(body, body_len) != stored) { if (error) snprintf(error, 256, "token: checksum mismatch"); return false; }

    bool ok = false;
    size_t pos = 0;
    uint32_t version, n_merges, n_special;
    if (!wire_read_u32(body, body_len, &pos, &version) || version != 1) { if (error) snprintf(error, 256, "token: bad version"); goto done; }
    if (!wire_read_u32(body, body_len, &pos, &n_merges)) { if (error) snprintf(error, 256, "token: truncated"); goto done; }
    if (!wire_read_u32(body, body_len, &pos, &n_special)) { if (error) snprintf(error, 256, "token: truncated"); goto done; }
    if (n_special > TOKEN_MAX_SPECIAL || TOKEN_BASE + (uint64_t)n_merges + n_special > TOKEN_MAX_VOCAB) {
        if (error) snprintf(error, 256, "token: vocab exceeds cap"); goto done;
    }

    if (n_merges) {
        t->merge_left = malloc(n_merges * sizeof(uint32_t));
        t->merge_right = malloc(n_merges * sizeof(uint32_t));
        if (!t->merge_left || !t->merge_right) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
    }
    for (uint32_t i = 0; i < n_merges; ++i) {
        if (!wire_read_u32(body, body_len, &pos, &t->merge_left[i]) || !wire_read_u32(body, body_len, &pos, &t->merge_right[i])) {
            if (error) snprintf(error, 256, "token: truncated merges"); goto done;
        }
        if (t->merge_left[i] >= TOKEN_BASE + i || t->merge_right[i] >= TOKEN_BASE + i) {
            if (error) snprintf(error, 256, "token: merge references a later or unknown token"); goto done;
        }
    }
    t->n_merges = n_merges;

    if (n_special) {
        t->special = calloc(n_special, sizeof(char *));
        if (!t->special) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
    }
    for (uint32_t i = 0; i < n_special; ++i) {
        uint32_t n;
        if (!wire_read_u32(body, body_len, &pos, &n) || pos + n > body_len) { if (error) snprintf(error, 256, "token: truncated special"); goto done; }
        t->special[i] = malloc(n + 1);
        if (!t->special[i]) { if (error) snprintf(error, 256, "token: OOM"); goto done; }
        memcpy(t->special[i], body + pos, n);
        t->special[i][n] = 0;
        t->n_special++;
        pos += n;
    }
    if (pos != body_len) { if (error) snprintf(error, 256, "token: trailing bytes"); goto done; }

    if (!finalize(t, error)) goto done;
    ok = true;

done:
    if (!ok) token_free(t);
    return ok;
}
