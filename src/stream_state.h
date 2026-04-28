#ifndef STREAM_STATE_H
#define STREAM_STATE_H

#include <stdint.h>
#include <stddef.h>

/**
 * State for streaming search across chunk boundaries.
 * The JS layer keeps needle.length - 1 bytes of carryover
 * and tracks the absolute byte offset.
 */
typedef struct {
    size_t total_bytes;  /* absolute byte position processed so far */
} stream_state_t;

#endif /* STREAM_STATE_H */