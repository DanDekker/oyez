#ifndef OYEZ_CORE_H
#define OYEZ_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <float.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OYEZ_C99_STATIC_ASSERT(cond, name) \
    typedef char oyez_c99_static_assert_##name[(cond) ? 1 : -1]

OYEZ_C99_STATIC_ASSERT(sizeof(uint64_t) == 8, uint64_is_8_bytes);
OYEZ_C99_STATIC_ASSERT(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53, ieee754_binary64);

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
void xml_escape(Buffer *buffer, const char *text, size_t length);
void xml_cstr(Buffer *buffer, const char *text);

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

static inline void mul64x64_128(uint64_t a, uint64_t b, uint64_t *out_lo, uint64_t *out_hi)
{
    uint64_t a_lo = a & 0xFFFFFFFFULL;
    uint64_t a_hi = a >> 32u;
    uint64_t b_lo = b & 0xFFFFFFFFULL;
    uint64_t b_hi = b >> 32u;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t mid1 = p1 + (p0 >> 32u);
    uint64_t mid2 = (mid1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);

    *out_lo = (p0 & 0xFFFFFFFFULL) | (mid2 << 32u);
    *out_hi = p3 + (mid1 >> 32u) + (p2 >> 32u) + (mid2 >> 32u);
}

static inline unsigned popcnt64(uint64_t x)
{
    x -= (x >> 1u) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2u) & 0x3333333333333333ULL);
    x = (x + (x >> 4u)) & 0x0F0F0F0F0F0F0F0FULL;
    return (unsigned)((x * 0x0101010101010101ULL) >> 56u);
}

static inline unsigned ctz64(uint64_t x)
{
    if (x == 0) return 64u;
    return popcnt64((x & (0ULL - x)) - 1ULL);
}

static inline unsigned clz64(uint64_t x)
{
    if (x == 0) return 64u;
    unsigned n = 0;
    if ((x & 0xFFFFFFFF00000000ULL) == 0) { n += 32u; x <<= 32u; }
    if ((x & 0xFFFF000000000000ULL) == 0) { n += 16u; x <<= 16u; }
    if ((x & 0xFF00000000000000ULL) == 0) { n += 8u; x <<= 8u; }
    if ((x & 0xF000000000000000ULL) == 0) { n += 4u; x <<= 4u; }
    if ((x & 0xC000000000000000ULL) == 0) { n += 2u; x <<= 2u; }
    if ((x & 0x8000000000000000ULL) == 0) { n += 1u; }
    return n;
}

static inline uint64_t rotl64(uint64_t x, unsigned k)
{
    unsigned s = k & 63u;
    return s == 0u ? x : ((x << s) | (x >> (64u - s)));
}

static inline uint64_t wyhash64(uint64_t a, uint64_t b)
{
    uint64_t r_lo = 0;
    uint64_t r_hi = 0;
    a ^= 0x2d358dccaa6c78a5ULL;
    b ^= 0x8bb84b93962eacc9ULL;
    mul64x64_128(a, b, &r_lo, &r_hi);
    return mix64(r_lo ^ r_hi);
}

/* ============================================================================
 * Pure First-Party ISO C99 IEEE-754 Binary64 Numerical & Transcendental Math
 * ============================================================================
 */
static inline uint64_t f64_to_bits(double x)
{
    uint64_t bits = 0;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static inline double f64_from_bits(uint64_t bits)
{
    double x = 0.0;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

static inline double f64_inf(void)
{
    return f64_from_bits(0x7FF0000000000000ULL);
}

static inline double f64_neg_inf(void)
{
    return f64_from_bits(0xFFF0000000000000ULL);
}

static inline int f64_is_inf(double x)
{
    return (f64_to_bits(x) & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
}

static inline int f64_is_nan(double x)
{
    uint64_t b = f64_to_bits(x);
    return ((b & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
           ((b & 0x000FFFFFFFFFFFFFULL) != 0ULL);
}

static inline int f64_is_finite(double x)
{
    return (f64_to_bits(x) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

static inline double f64_abs(double x)
{
    return f64_from_bits(f64_to_bits(x) & 0x7FFFFFFFFFFFFFFFULL);
}

static inline double f64_sqrt(double x)
{
    if (x <= 0.0) return 0.0;
    if (!f64_is_finite(x)) return x;
    uint64_t b = f64_to_bits(x);
    double r = f64_from_bits((b >> 1u) + 0x1FF8000000000000ULL);
    for (int i = 0; i < 6; ++i) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

static inline double f64_ln(double x)
{
    if (x <= 1e-300) return -690.7755278982137;
    if (!f64_is_finite(x)) return x;
    uint64_t b = f64_to_bits(x);
    int exp2 = (int)((b >> 52u) & 0x7FFu) - 1023;
    double m = f64_from_bits((b & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL);
    if (m > 1.4142135623730950488) {
        m *= 0.5;
        exp2++;
    }
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double poly = 1.0 / 17.0;
    poly = (1.0 / 15.0) + y2 * poly;
    poly = (1.0 / 13.0) + y2 * poly;
    poly = (1.0 / 11.0) + y2 * poly;
    poly = (1.0 / 9.0) + y2 * poly;
    poly = (1.0 / 7.0) + y2 * poly;
    poly = (1.0 / 5.0) + y2 * poly;
    poly = (1.0 / 3.0) + y2 * poly;
    poly = 1.0 + y2 * poly;
    return 2.0 * y * poly + (double)exp2 * 0.69314718055994530942;
}

static inline double f64_exp(double x)
{
    if (x <= -708.0) return 0.0;
    if (x >= 709.0) return f64_inf();
    int k = (int)(x * 1.44269504088896340736 + (x >= 0.0 ? 0.5 : -0.5));
    double r = x - (double)k * 0.69314718055994530942;
    double s = r * 0.25;
    double s2 = s * s;
    double s3 = s2 * s;
    double s4 = s2 * s2;
    double num = 1.0 + 0.5 * s + (3.0 / 28.0) * s2 + (1.0 / 84.0) * s3 + (1.0 / 1680.0) * s4;
    double den = 1.0 - 0.5 * s + (3.0 / 28.0) * s2 - (1.0 / 84.0) * s3 + (1.0 / 1680.0) * s4;
    double e = num / den;
    e *= e;
    e *= e;
    if (k < -1022) return 0.0;
    if (k > 1023) return f64_inf();
    double scale = f64_from_bits((uint64_t)(k + 1023) << 52u);
    return e * scale;
}

static inline double f64_atan(double x)
{
    if (f64_is_nan(x)) return x;
    int neg = 0;
    if (x < 0.0) {
        x = -x;
        neg = 1;
    }
    int inv = 0;
    if (x > 1.0) {
        x = 1.0 / x;
        inv = 1;
    }
    for (int k = 0; k < 3; ++k) {
        x = x / (1.0 + f64_sqrt(1.0 + x * x));
    }
    double x2 = x * x;
    double poly = -1.0 / 11.0;
    poly = (1.0 / 9.0) + x2 * poly;
    poly = (-1.0 / 7.0) + x2 * poly;
    poly = (1.0 / 5.0) + x2 * poly;
    poly = (-1.0 / 3.0) + x2 * poly;
    poly = 1.0 + x2 * poly;
    double angle = 8.0 * x * poly;
    if (inv) angle = 1.57079632679489661923 - angle;
    return neg ? -angle : angle;
}

static inline double f64_cos(double x)
{
    x = f64_abs(x);
    double turns = (double)(int64_t)(x * 0.15915494309189533577);
    x -= turns * 6.28318530717958647692;
    if (x > 3.14159265358979323846) {
        x = 6.28318530717958647692 - x;
    }
    double t = x * 0.125;
    double t2 = t * t;
    double c = 1.0 - t2 * (0.5 - t2 * ((1.0 / 24.0) - t2 * ((1.0 / 720.0) -
               t2 * ((1.0 / 40320.0) - t2 * (1.0 / 3628800.0)))));
    c = 2.0 * c * c - 1.0;
    c = 2.0 * c * c - 1.0;
    c = 2.0 * c * c - 1.0;
    return c;
}

#ifdef __cplusplus
}
#endif

#endif
