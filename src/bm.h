#ifndef BM_H
#define BM_H

#include <stdint.h>
#include <stddef.h>

#define BM_ALPHABET_SIZE 256
#define BM_LINEAR_THRESHOLD 4

typedef struct {
    const uint8_t *pattern;
    size_t pattern_len;
    int64_t bad_char[BM_ALPHABET_SIZE];
    int64_t *good_suffix;
} bm_context_t;

/**
 * Compile a Boyer-Moore search context for the given pattern.
 * Returns NULL on allocation failure.
 */
bm_context_t *bm_compile(const uint8_t *pattern, size_t pattern_len);

/**
 * Search haystack for all occurrences of the compiled pattern.
 * Results are written to `results` (caller-allocated), up to `max_results`.
 * Returns the number of matches found.
 */
size_t bm_search(
    const bm_context_t *ctx,
    const uint8_t *haystack,
    size_t haystack_len,
    size_t start_offset,
    int overlap,
    int64_t *results,
    size_t max_results
);

/**
 * Count occurrences without storing offsets.
 */
size_t bm_count(
    const bm_context_t *ctx,
    const uint8_t *haystack,
    size_t haystack_len,
    size_t start_offset,
    int overlap
);

/**
 * Free a compiled context.
 */
void bm_free(bm_context_t *ctx);

#endif /* BM_H */