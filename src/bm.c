#include "bm.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Platform detection                                                 */
/* ================================================================== */

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define BM_ARCH_X86 1
  #ifdef _MSC_VER
    #include <intrin.h>
  #else
    #include <emmintrin.h>
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define BM_ARCH_ARM64 1
  #include <arm_neon.h>
#endif

/* ================================================================== */
/*  Bit helpers                                                        */
/* ================================================================== */

#ifdef BM_ARCH_X86
static __inline int bm_ctz32(uint32_t v) {
#ifdef _MSC_VER
    unsigned long idx;
    _BitScanForward(&idx, v);
    return (int)idx;
#else
    return __builtin_ctz(v);
#endif
}
#endif /* BM_ARCH_X86 */

/* ================================================================== */
/*  Rare-byte selection                                                */
/*  Pick the byte in the pattern that is statistically rarest in       */
/*  generic binary data.  We use a fixed heuristic table: bytes        */
/*  0x20-0x7E (printable ASCII) and 0x00 are common; others are rare.  */
/*  Returns the INDEX within the pattern of the rarest byte.           */
/* ================================================================== */

static size_t pick_rare_index(const uint8_t *pat, size_t m)
{
    /*
     * Simple heuristic: prefer bytes outside printable ASCII and 0x00,
     * break ties by preferring a position near the end of the pattern
     * (lets us skip more on mismatch).
     */
    static const uint8_t rarity[256] = {
        /* 0x00 */ 1,
        /* 0x01-0x1F (control chars) */
        3,3,3,3,3,3,3,3,3,2,2,3,3,2,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        /* 0x20-0x7E (printable ASCII) */
        1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        /* 0x7F */ 3,
        /* 0x80-0xFF (high bytes — rarest in typical data) */
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
    };

    size_t best = m - 1;          /* default: last byte */
    uint8_t best_score = rarity[pat[best]];
    size_t i;

    /* Walk backwards so we prefer positions closer to the end on ties */
    for (i = m - 1; i-- > 0; ) {
        uint8_t s = rarity[pat[i]];
        if (s > best_score) {
            best_score = s;
            best = i;
        }
    }
    return best;
}

/* ================================================================== */
/*  1-byte pattern — direct memchr (CRT SIMD-optimized)                */
/* ================================================================== */

static size_t search_1byte(uint8_t byte,
                           const uint8_t *hay, size_t n,
                           size_t start, int overlap,
                           int64_t *results, size_t max_results)
{
    size_t count = 0;
    const uint8_t *p = hay + start;
    const uint8_t *end = hay + n;

    (void)overlap; /* always 1 for single byte */

    while (p < end) {
        p = (const uint8_t *)memchr(p, byte, (size_t)(end - p));
        if (!p) break;
        if (results) results[count] = (int64_t)(p - hay);
        count++;
        if (results && count >= max_results) return count;
        p++;
    }
    return count;
}

static size_t count_1byte(uint8_t byte,
                          const uint8_t *hay, size_t n,
                          size_t start)
{
    size_t count = 0;
    const uint8_t *p = hay + start;
    const uint8_t *end = hay + n;

    while (p < end) {
        p = (const uint8_t *)memchr(p, byte, (size_t)(end - p));
        if (!p) break;
        count++;
        p++;
    }
    return count;
}

/* ================================================================== */
/*  SSE2 dual-byte SIMD search (x86 / x86_64)                         */
/*                                                                     */
/*  For each 16-byte block, simultaneously check if pattern[0] and     */
/*  pattern[m-1] both appear at the correct distance.  On a hit,       */
/*  verify the interior with memcmp.  This reduces candidates to       */
/*  ~1/65536 of all positions.                                         */
/* ================================================================== */

#ifdef BM_ARCH_X86

static size_t simd_search(const uint8_t * __restrict pat, size_t m,
                          const uint8_t * __restrict hay, size_t n,
                          size_t start, int overlap,
                          int64_t *results, size_t max_results)
{
    const __m128i vfirst = _mm_set1_epi8((char)pat[0]);
    const __m128i vlast  = _mm_set1_epi8((char)pat[m - 1]);
    size_t count = 0;
    size_t skip = start;
    size_t i;

    for (i = start; i + m + 15 <= n; i += 16) {
        const __m128i cf = _mm_loadu_si128((const __m128i *)(hay + i));
        const __m128i cl = _mm_loadu_si128((const __m128i *)(hay + i + m - 1));

        int mask = _mm_movemask_epi8(
            _mm_and_si128(_mm_cmpeq_epi8(vfirst, cf),
                          _mm_cmpeq_epi8(vlast, cl)));

        while (mask) {
            int bit = bm_ctz32((uint32_t)mask);
            mask &= mask - 1;
            size_t pos = i + (size_t)bit;

            if (pos < skip) continue;

            /* For 2-byte patterns the first+last check is the full match */
            if (m == 2 ||
                memcmp(hay + pos + 1, pat + 1, m - 2) == 0)
            {
                if (results) results[count] = (int64_t)pos;
                count++;
                if (results && count >= max_results) return count;
                if (!overlap) skip = pos + m;
            }
        }
    }

    /* Scalar tail for remaining bytes */
    for (i = (i < skip) ? skip : i; i + m <= n; i++) {
        if (hay[i] == pat[0] && hay[i + m - 1] == pat[m - 1] &&
            (m <= 2 || memcmp(hay + i + 1, pat + 1, m - 2) == 0))
        {
            if (results) results[count] = (int64_t)i;
            count++;
            if (results && count >= max_results) return count;
            if (!overlap) i += m - 1;
        }
    }

    return count;
}

static size_t simd_count(const uint8_t * __restrict pat, size_t m,
                         const uint8_t * __restrict hay, size_t n,
                         size_t start, int overlap)
{
    const __m128i vfirst = _mm_set1_epi8((char)pat[0]);
    const __m128i vlast  = _mm_set1_epi8((char)pat[m - 1]);
    size_t count = 0;
    size_t skip = start;
    size_t i;

    for (i = start; i + m + 15 <= n; i += 16) {
        const __m128i cf = _mm_loadu_si128((const __m128i *)(hay + i));
        const __m128i cl = _mm_loadu_si128((const __m128i *)(hay + i + m - 1));

        int mask = _mm_movemask_epi8(
            _mm_and_si128(_mm_cmpeq_epi8(vfirst, cf),
                          _mm_cmpeq_epi8(vlast, cl)));

        while (mask) {
            int bit = bm_ctz32((uint32_t)mask);
            mask &= mask - 1;
            size_t pos = i + (size_t)bit;

            if (pos < skip) continue;

            if (m == 2 ||
                memcmp(hay + pos + 1, pat + 1, m - 2) == 0)
            {
                count++;
                if (!overlap) skip = pos + m;
            }
        }
    }

    for (i = (i < skip) ? skip : i; i + m <= n; i++) {
        if (hay[i] == pat[0] && hay[i + m - 1] == pat[m - 1] &&
            (m <= 2 || memcmp(hay + i + 1, pat + 1, m - 2) == 0))
        {
            count++;
            if (!overlap) i += m - 1;
        }
    }

    return count;
}

#endif /* BM_ARCH_X86 */

/* ================================================================== */
/*  NEON dual-byte SIMD search (ARM64)                                 */
/* ================================================================== */

#ifdef BM_ARCH_ARM64

static size_t neon_search(const uint8_t * __restrict pat, size_t m,
                          const uint8_t * __restrict hay, size_t n,
                          size_t start, int overlap,
                          int64_t *results, size_t max_results)
{
    const uint8x16_t vfirst = vdupq_n_u8(pat[0]);
    const uint8x16_t vlast  = vdupq_n_u8(pat[m - 1]);
    size_t count = 0;
    size_t skip = start;
    size_t i;
    uint8_t tmp[16];

    for (i = start; i + m + 15 <= n; i += 16) {
        uint8x16_t cf = vld1q_u8(hay + i);
        uint8x16_t cl = vld1q_u8(hay + i + m - 1);

        uint8x16_t combined = vandq_u8(vceqq_u8(vfirst, cf),
                                       vceqq_u8(vlast, cl));

        /* Fast reject — no matches in this block */
        if (vmaxvq_u8(combined) == 0) continue;

        vst1q_u8(tmp, combined);
        for (int j = 0; j < 16; j++) {
            if (!tmp[j]) continue;
            size_t pos = i + (size_t)j;
            if (pos < skip) continue;

            if (m == 2 ||
                memcmp(hay + pos + 1, pat + 1, m - 2) == 0)
            {
                if (results) results[count] = (int64_t)pos;
                count++;
                if (results && count >= max_results) return count;
                if (!overlap) skip = pos + m;
            }
        }
    }

    for (i = (i < skip) ? skip : i; i + m <= n; i++) {
        if (hay[i] == pat[0] && hay[i + m - 1] == pat[m - 1] &&
            (m <= 2 || memcmp(hay + i + 1, pat + 1, m - 2) == 0))
        {
            if (results) results[count] = (int64_t)i;
            count++;
            if (results && count >= max_results) return count;
            if (!overlap) i += m - 1;
        }
    }

    return count;
}

static size_t neon_count(const uint8_t * __restrict pat, size_t m,
                         const uint8_t * __restrict hay, size_t n,
                         size_t start, int overlap)
{
    const uint8x16_t vfirst = vdupq_n_u8(pat[0]);
    const uint8x16_t vlast  = vdupq_n_u8(pat[m - 1]);
    size_t count = 0;
    size_t skip = start;
    size_t i;
    uint8_t tmp[16];

    for (i = start; i + m + 15 <= n; i += 16) {
        uint8x16_t cf = vld1q_u8(hay + i);
        uint8x16_t cl = vld1q_u8(hay + i + m - 1);

        uint8x16_t combined = vandq_u8(vceqq_u8(vfirst, cf),
                                       vceqq_u8(vlast, cl));

        if (vmaxvq_u8(combined) == 0) continue;

        vst1q_u8(tmp, combined);
        for (int j = 0; j < 16; j++) {
            if (!tmp[j]) continue;
            size_t pos = i + (size_t)j;
            if (pos < skip) continue;

            if (m == 2 ||
                memcmp(hay + pos + 1, pat + 1, m - 2) == 0)
            {
                count++;
                if (!overlap) skip = pos + m;
            }
        }
    }

    for (i = (i < skip) ? skip : i; i + m <= n; i++) {
        if (hay[i] == pat[0] && hay[i + m - 1] == pat[m - 1] &&
            (m <= 2 || memcmp(hay + i + 1, pat + 1, m - 2) == 0))
        {
            count++;
            if (!overlap) i += m - 1;
        }
    }

    return count;
}

#endif /* BM_ARCH_ARM64 */

/* ================================================================== */
/*  Generic fallback: memchr on rare byte + Raita precheck             */
/*                                                                     */
/*  1. Pick the rarest byte in the pattern                             */
/*  2. Use memchr (CRT SIMD-optimized) to find that byte               */
/*  3. Verify first, middle, last bytes (Raita)                        */
/*  4. Full memcmp only if all three pass                              */
/* ================================================================== */

static size_t generic_search(const uint8_t *pat, size_t m,
                             size_t rare_idx,
                             const uint8_t *hay, size_t n,
                             size_t start, int overlap,
                             int64_t *results, size_t max_results)
{
    const uint8_t rare_byte = pat[rare_idx];
    const uint8_t first = pat[0];
    const uint8_t last  = pat[m - 1];
    const uint8_t mid   = pat[m / 2];
    const size_t mid_idx = m / 2;
    size_t count = 0;

    /* Scan from (start + rare_idx) so candidate aligns pat[rare_idx] */
    const uint8_t *p = hay + start + rare_idx;
    const uint8_t *end = hay + n - m + 1 + rare_idx;

    while (p < end) {
        p = (const uint8_t *)memchr(p, rare_byte, (size_t)(end - p));
        if (!p) break;

        /* candidate start = p - rare_idx */
        const uint8_t *cand = p - rare_idx;

        /* Raita: first, last, middle precheck */
        if (cand[0] == first && cand[m - 1] == last && cand[mid_idx] == mid) {
            /* Full verify */
            if (memcmp(cand, pat, m) == 0) {
                if (results) results[count] = (int64_t)(cand - hay);
                count++;
                if (results && count >= max_results) return count;
                if (!overlap) {
                    p = cand + m + rare_idx;
                    continue;
                }
            }
        }
        p++;
    }

    return count;
}

static size_t generic_count(const uint8_t *pat, size_t m,
                            size_t rare_idx,
                            const uint8_t *hay, size_t n,
                            size_t start, int overlap)
{
    const uint8_t rare_byte = pat[rare_idx];
    const uint8_t first = pat[0];
    const uint8_t last  = pat[m - 1];
    const uint8_t mid   = pat[m / 2];
    const size_t mid_idx = m / 2;
    size_t count = 0;

    const uint8_t *p = hay + start + rare_idx;
    const uint8_t *end = hay + n - m + 1 + rare_idx;

    while (p < end) {
        p = (const uint8_t *)memchr(p, rare_byte, (size_t)(end - p));
        if (!p) break;

        const uint8_t *cand = p - rare_idx;

        if (cand[0] == first && cand[m - 1] == last && cand[mid_idx] == mid) {
            if (memcmp(cand, pat, m) == 0) {
                count++;
                if (!overlap) {
                    p = cand + m + rare_idx;
                    continue;
                }
            }
        }
        p++;
    }

    return count;
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

bm_context_t *bm_compile(const uint8_t *pattern, size_t pattern_len)
{
    bm_context_t *ctx;

    if (pattern_len == 0)
        return NULL;

    ctx = (bm_context_t *)calloc(1, sizeof(bm_context_t));
    if (!ctx)
        return NULL;

    ctx->pattern = pattern;
    ctx->pattern_len = pattern_len;

    return ctx;
}

size_t bm_search(const bm_context_t *ctx,
                 const uint8_t *haystack, size_t haystack_len,
                 size_t start_offset, int overlap,
                 int64_t *results, size_t max_results)
{
    const uint8_t *pat;
    size_t m;

    if (!ctx || !haystack || max_results == 0)
        return 0;

    pat = ctx->pattern;
    m = ctx->pattern_len;

    if (haystack_len < m || start_offset > haystack_len - m)
        return 0;

    /* 1-byte: direct memchr */
    if (m == 1)
        return search_1byte(pat[0], haystack, haystack_len,
                            start_offset, overlap, results, max_results);

#if defined(BM_ARCH_X86)
    return simd_search(pat, m, haystack, haystack_len,
                       start_offset, overlap, results, max_results);
#elif defined(BM_ARCH_ARM64)
    return neon_search(pat, m, haystack, haystack_len,
                       start_offset, overlap, results, max_results);
#else
    {
        size_t rare = pick_rare_index(pat, m);
        return generic_search(pat, m, rare, haystack, haystack_len,
                              start_offset, overlap, results, max_results);
    }
#endif
}

size_t bm_count(const bm_context_t *ctx,
                const uint8_t *haystack, size_t haystack_len,
                size_t start_offset, int overlap)
{
    const uint8_t *pat;
    size_t m;

    if (!ctx || !haystack)
        return 0;

    pat = ctx->pattern;
    m = ctx->pattern_len;

    if (haystack_len < m || start_offset > haystack_len - m)
        return 0;

    if (m == 1)
        return count_1byte(pat[0], haystack, haystack_len, start_offset);

#if defined(BM_ARCH_X86)
    return simd_count(pat, m, haystack, haystack_len,
                      start_offset, overlap);
#elif defined(BM_ARCH_ARM64)
    return neon_count(pat, m, haystack, haystack_len,
                      start_offset, overlap);
#else
    {
        size_t rare = pick_rare_index(pat, m);
        return generic_count(pat, m, rare, haystack, haystack_len,
                             start_offset, overlap);
    }
#endif
}

void bm_free(bm_context_t *ctx)
{
    if (!ctx)
        return;
    free(ctx);
}
