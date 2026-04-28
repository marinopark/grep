#include "bm.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void build_bad_char(int64_t table[BM_ALPHABET_SIZE],
                           const uint8_t *pattern, size_t m)
{
    size_t i;
    for (i = 0; i < BM_ALPHABET_SIZE; i++)
        table[i] = (int64_t)m;

    for (i = 0; i < m - 1; i++)
        table[pattern[i]] = (int64_t)(m - 1 - i);
}

/*
 * Compute the suffix array used by the good-suffix rule.
 * suffix[i] = length of the longest suffix of pattern[0..i]
 *             that is also a suffix of pattern[0..m-1].
 */
static void compute_suffixes(const uint8_t *pattern, size_t m,
                             int64_t *suffix)
{
    int64_t im = (int64_t)m;
    int64_t f = 0, g, i;

    suffix[im - 1] = im;
    g = im - 1;

    for (i = im - 2; i >= 0; i--) {
        if (i > g && suffix[i + im - 1 - f] < i - g) {
            suffix[i] = suffix[i + im - 1 - f];
        } else {
            if (i < g)
                g = i;
            f = i;
            while (g >= 0 && pattern[g] == pattern[g + im - 1 - f])
                g--;
            suffix[i] = f - g;
        }
    }
}

static void build_good_suffix(int64_t *gs, const uint8_t *pattern, size_t m)
{
    int64_t im = (int64_t)m;
    int64_t i, j;
    int64_t *suffix;

    suffix = (int64_t *)malloc(m * sizeof(int64_t));
    if (!suffix) {
        /* Fallback: fill with pattern length (safe but slower) */
        for (i = 0; i < im; i++)
            gs[i] = im;
        return;
    }

    compute_suffixes(pattern, m, suffix);

    /* Default shift: full pattern length */
    for (i = 0; i < im; i++)
        gs[i] = im;

    /* Case 1: matching suffix is a prefix of the pattern */
    j = 0;
    for (i = im - 1; i >= 0; i--) {
        if (suffix[i] == i + 1) {
            for (; j < im - 1 - i; j++) {
                if (gs[j] == im)
                    gs[j] = im - 1 - i;
            }
        }
    }

    /* Case 2: matching suffix occurs elsewhere in the pattern */
    for (i = 0; i <= im - 2; i++) {
        gs[im - 1 - suffix[i]] = im - 1 - i;
    }

    free(suffix);
}

/* ------------------------------------------------------------------ */
/*  Linear (brute-force) search for short patterns (len <= 4)          */
/* ------------------------------------------------------------------ */

static size_t linear_search(const uint8_t *pattern, size_t m,
                            const uint8_t *haystack, size_t n,
                            size_t start, int overlap,
                            int64_t *results, size_t max_results)
{
    size_t count = 0;
    size_t i;

    if (m == 0 || n < m || start > n - m)
        return 0;

    for (i = start; i <= n - m; i++) {
        if (memcmp(haystack + i, pattern, m) == 0) {
            if (results && count < max_results)
                results[count] = (int64_t)i;
            count++;
            if (results && count >= max_results)
                return count;
            if (!overlap)
                i += m - 1; /* will be incremented by loop */
        }
    }
    return count;
}

static size_t linear_count(const uint8_t *pattern, size_t m,
                           const uint8_t *haystack, size_t n,
                           size_t start, int overlap)
{
    size_t count = 0;
    size_t i;

    if (m == 0 || n < m || start > n - m)
        return 0;

    for (i = start; i <= n - m; i++) {
        if (memcmp(haystack + i, pattern, m) == 0) {
            count++;
            if (!overlap)
                i += m - 1;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

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

    /* For very short patterns, skip BM preprocessing */
    if (pattern_len <= BM_LINEAR_THRESHOLD) {
        ctx->good_suffix = NULL;
        return ctx;
    }

    build_bad_char(ctx->bad_char, pattern, pattern_len);

    ctx->good_suffix = (int64_t *)malloc(pattern_len * sizeof(int64_t));
    if (!ctx->good_suffix) {
        free(ctx);
        return NULL;
    }
    build_good_suffix(ctx->good_suffix, pattern, pattern_len);

    return ctx;
}

size_t bm_search(const bm_context_t *ctx,
                 const uint8_t *haystack, size_t haystack_len,
                 size_t start_offset, int overlap,
                 int64_t *results, size_t max_results)
{
    const uint8_t *pat;
    size_t m, count;
    int64_t im, j, shift_bc, shift_gs;

    if (!ctx || !haystack || max_results == 0)
        return 0;

    pat = ctx->pattern;
    m = ctx->pattern_len;

    /* Short pattern: linear scan */
    if (m <= BM_LINEAR_THRESHOLD) {
        return linear_search(pat, m, haystack, haystack_len,
                             start_offset, overlap, results, max_results);
    }

    if (haystack_len < m || start_offset > haystack_len - m)
        return 0;

    im = (int64_t)m;
    count = 0;
    j = (int64_t)start_offset;

    while (j <= (int64_t)(haystack_len - m)) {
        int64_t i = im - 1;

        while (i >= 0 && pat[i] == haystack[j + i])
            i--;

        if (i < 0) {
            /* Match found */
            results[count] = j;
            count++;
            if (count >= max_results)
                return count;

            if (overlap)
                j += 1;
            else
                j += im;
        } else {
            shift_bc = ctx->bad_char[haystack[j + i]] - (im - 1 - i);
            shift_gs = ctx->good_suffix[i];
            j += (shift_bc > shift_gs) ? shift_bc : shift_gs;
        }
    }

    return count;
}

size_t bm_count(const bm_context_t *ctx,
                const uint8_t *haystack, size_t haystack_len,
                size_t start_offset, int overlap)
{
    const uint8_t *pat;
    size_t m, count;
    int64_t im, j, shift_bc, shift_gs;

    if (!ctx || !haystack)
        return 0;

    pat = ctx->pattern;
    m = ctx->pattern_len;

    /* Short pattern: linear count */
    if (m <= BM_LINEAR_THRESHOLD) {
        return linear_count(pat, m, haystack, haystack_len,
                            start_offset, overlap);
    }

    if (haystack_len < m || start_offset > haystack_len - m)
        return 0;

    im = (int64_t)m;
    count = 0;
    j = (int64_t)start_offset;

    while (j <= (int64_t)(haystack_len - m)) {
        int64_t i = im - 1;

        while (i >= 0 && pat[i] == haystack[j + i])
            i--;

        if (i < 0) {
            count++;
            if (overlap)
                j += 1;
            else
                j += im;
        } else {
            shift_bc = ctx->bad_char[haystack[j + i]] - (im - 1 - i);
            shift_gs = ctx->good_suffix[i];
            j += (shift_bc > shift_gs) ? shift_bc : shift_gs;
        }
    }

    return count;
}

void bm_free(bm_context_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->good_suffix)
        free(ctx->good_suffix);
    free(ctx);
}
