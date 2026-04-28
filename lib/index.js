'use strict';

const path = require('path');
const { Transform } = require('stream');

const native = require(path.resolve(__dirname, '..', 'build', 'Release', 'grep_native.node'));

function toBuffer(needle) {
    if (Buffer.isBuffer(needle)) return needle;
    if (typeof needle === 'string') return Buffer.from(needle, 'utf8');
    throw new TypeError('needle must be a Buffer or string');
}

/**
 * Synchronously search for all occurrences of `needle` in `haystack`.
 * @param {Buffer} haystack
 * @param {Buffer|string} needle
 * @param {{ overlap?: boolean, offset?: number, limit?: number }} [options]
 * @returns {number[]} array of byte offsets
 */
function search(haystack, needle, options) {
    if (!Buffer.isBuffer(haystack))
        throw new TypeError('haystack must be a Buffer');
    const buf = toBuffer(needle);
    if (buf.length === 0)
        throw new RangeError('needle must not be empty');
    return native.search(haystack, buf, options || {});
}

/**
 * Asynchronously search (runs on a worker thread).
 * @param {Buffer} haystack
 * @param {Buffer|string} needle
 * @param {{ overlap?: boolean, offset?: number, limit?: number }} [options]
 * @returns {Promise<number[]>}
 */
function searchAsync(haystack, needle, options) {
    if (!Buffer.isBuffer(haystack))
        return Promise.reject(new TypeError('haystack must be a Buffer'));
    const buf = toBuffer(needle);
    if (buf.length === 0)
        return Promise.reject(new RangeError('needle must not be empty'));
    return native.searchAsync(haystack, buf, options || {});
}

/**
 * Count occurrences without allocating an offset array.
 * @param {Buffer} haystack
 * @param {Buffer|string} needle
 * @param {{ overlap?: boolean, offset?: number }} [options]
 * @returns {number}
 */
function count(haystack, needle, options) {
    if (!Buffer.isBuffer(haystack))
        throw new TypeError('haystack must be a Buffer');
    const buf = toBuffer(needle);
    if (buf.length === 0)
        throw new RangeError('needle must not be empty');
    return native.count(haystack, buf, options || {});
}

/**
 * Create a Transform stream that emits 'match' events with absolute offsets.
 * @param {Buffer|string} needle
 * @param {{ overlap?: boolean }} [options]
 * @returns {Transform}
 */
function createSearchStream(needle, options) {
    const pat = toBuffer(needle);
    if (pat.length === 0)
        throw new RangeError('needle must not be empty');

    const overlap = !!(options && options.overlap);
    let carryover = Buffer.alloc(0);
    let totalBytes = 0;

    const stream = new Transform({
        ...(options || {}),
        transform(chunk, encoding, callback) {
            const buf = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk, encoding);

            /* Combine carryover with new chunk */
            const combined = carryover.length > 0
                ? Buffer.concat([carryover, buf])
                : buf;

            /* Search the combined buffer */
            const offsets = native.search(combined, pat, { overlap });

            /* Emit match events with absolute offsets */
            const carryLen = carryover.length;
            for (const off of offsets) {
                const absoluteOffset = totalBytes - carryLen + off;
                stream.emit('match', absoluteOffset);
            }

            /* Keep the last (pat.length - 1) bytes as carryover */
            const keep = pat.length - 1;
            if (combined.length > keep) {
                carryover = combined.subarray(combined.length - keep);
            } else {
                carryover = combined;
            }

            totalBytes += buf.length;

            /* Pass through the original chunk */
            this.push(buf);
            callback();
        },

        flush(callback) {
            carryover = Buffer.alloc(0);
            callback();
        }
    });

    return stream;
}

module.exports = { search, searchAsync, count, createSearchStream };
