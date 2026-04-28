[![npm version](https://img.shields.io/npm/v/@marinopark/grep.svg)](https://www.npmjs.com/package/@marinopark/grep)
[![CI](https://github.com/marinopark/grep/actions/workflows/ci.yml/badge.svg)](https://github.com/marinopark/grep/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Node.js Version](https://img.shields.io/node/v/@marinopark/grep.svg)](https://nodejs.org)

# @marinopark/grep

SIMD-accelerated native C binary pattern search for Node.js Buffers. **11x faster** than `Buffer.indexOf` loops on real-world data.

## Why?

The existing npm ecosystem lacks a package that combines **N-API C** with **raw Buffer** support and **SIMD acceleration**:

| Package | N-API C | Buffer support | SIMD | Algorithm |
|---------|---------|----------------|------|-----------|
| `fast-string-search` | Yes | No (UTF-16 only) | No | Boyer-Moore |
| `bop` | No (pure JS) | Yes | No | Boyer-Moore |
| `streamsearch` | No (pure JS) | Yes (stream) | No | Boyer-Moore-Horspool |
| **`@marinopark/grep`** | **Yes** | **Yes (zero-copy)** | **SSE2 / NEON** | **SIMD dual-byte filter** |

This package runs a SIMD-accelerated search directly on `uint8_t*` data from Node.js Buffers via N-API — no string conversion, no copying.

## Installation

```bash
npm install @marinopark/grep
```

**Requirements:** A C compiler toolchain for your platform (node-gyp builds from source).
- **macOS:** Xcode Command Line Tools (`xcode-select --install`)
- **Linux:** `build-essential` (GCC/G++, make)
- **Windows:** Visual Studio Build Tools with C++ workload

## Quick Start

```js
const grep = require('@marinopark/grep');
const fs = require('fs');

// Search a memory dump for a string
const dump = fs.readFileSync('memdump.bin');
const offsets = grep.search(dump, Buffer.from('PASSWORD'));
// [1024, 8192, 32768]

// Search for hex patterns
const offsets2 = grep.search(dump, Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]));

// Count occurrences without allocating offset array
const count = grep.count(dump, Buffer.from('SECRET'));

// Async search (doesn't block the event loop)
const asyncOffsets = await grep.searchAsync(dump, 'PASSWORD');

// Stream search for huge files
const searchStream = grep.createSearchStream(Buffer.from('SECRET'));
searchStream.on('match', (offset) => console.log(`Found at byte ${offset}`));
fs.createReadStream('huge_dump.bin').pipe(searchStream);
```

## API Reference

### `grep.search(haystack, needle, options?)`

Synchronously search for all occurrences of `needle` in `haystack`.

- **haystack** `Buffer` — data to search in
- **needle** `Buffer | string` — pattern to find
- **options** `object`
  - `overlap` `boolean` (default: `false`) — include overlapping matches
  - `offset` `number` (default: `0`) — byte offset to start searching from
  - `limit` `number` (default: `Infinity`) — maximum number of results
- **Returns** `number[]` — array of byte offsets where the pattern was found

### `grep.searchAsync(haystack, needle, options?)`

Same as `search` but runs on a worker thread via `napi_async_work`. Returns a `Promise<number[]>`.

Use this for large haystacks to avoid blocking the event loop.

### `grep.count(haystack, needle, options?)`

Count occurrences without allocating an offset array. More memory-efficient when you only need the count.

- Same arguments as `search` (except `limit` is ignored)
- **Returns** `number`

### `grep.createSearchStream(needle, options?)`

Create a `Transform` stream that emits `'match'` events with absolute byte offsets.

- **needle** `Buffer | string`
- **options** `object`
  - `overlap` `boolean` (default: `false`)
- **Returns** `Transform` stream

The stream automatically handles pattern matches that span chunk boundaries by keeping a carryover buffer of `needle.length - 1` bytes.

## Algorithm

The search engine selects the best strategy based on platform and pattern length:

### SIMD dual-byte filter (x86 SSE2 / ARM64 NEON)

The primary search path on modern hardware. For each 16-byte block of the haystack:

1. Load 16 bytes at position `i` and 16 bytes at position `i + m - 1`
2. Compare all 16 bytes against `pattern[0]` and `pattern[m-1]` simultaneously
3. AND the two result masks — only positions where **both** first and last bytes match survive
4. Verify surviving candidates with `memcmp`

This reduces candidates to ~1/65,536 of all positions, making the search effectively **O(n/16)** per SIMD iteration with very few false positives.

### Generic fallback (memchr + Raita precheck)

On platforms without SIMD:

1. **Rare-byte selection** — statistically pick the rarest byte in the pattern to minimize false positives
2. **memchr scan** — use the CRT's SIMD-optimized `memchr` to find the rare byte (fast even without explicit SIMD)
3. **Raita precheck** — verify first, middle, and last bytes before a full `memcmp`

### 1-byte patterns

Dispatched directly to `memchr` (SIMD-optimized by the C runtime on all platforms).

## Benchmarks

Run benchmarks with:

```bash
npm run bench
```

**Real-world test: 240MB Zalo memory dump, searching for `"cipherKey"` (9 bytes, 8 matches)**

| Method | Median | vs grep.search |
|--------|--------|----------------|
| `Buffer.indexOf` loop | 89.7ms | 11.4x slower |
| **`grep.search`** | **7.9ms** | — |
| `grep.count` | 7.1ms | 1.1x faster |

**Pattern length comparison (same 240MB dump)**

| Pattern | Buffer.indexOf loop | grep.search | Speedup |
|---------|-------------------|-------------|---------|
| 2-byte `0x0001` | 435ms | 61ms | **7.2x** |
| 9-byte `cipherKey` | 113ms | 13ms | **8.6x** |
| 16-byte ASCII | 69ms | 15ms | **4.7x** |
| No match (17-byte) | 59ms | 17ms | **3.4x** |

**Synthetic benchmark (`npm run bench`)**

| Scenario | Buffer.indexOf loop | grep.search | Result |
|----------|-------------------|-------------|--------|
| 1KB, 4B pattern (frequent) | 0.002ms | 0.002ms | ~tie |
| 1MB, 16B pattern (frequent) | 0.066ms | 0.078ms | ~tie |
| 100MB, 64B pattern | 2.89ms | 3.38ms | ~tie |
| 1MB, 1-byte 0x00 (worst case) | 51.5ms | 58.8ms | ~tie |
| 1MB, no match | 0.027ms | 0.050ms | ~tie |

> The SIMD advantage scales with buffer size and is most pronounced with **sparse matches in large buffers** (the real-world use case). In synthetic benchmarks with artificially frequent matches, both approaches are comparable.

*(Measured on Windows 11, Node.js 22, AMD64. Run `npm run bench` on your machine for your own results.)*

## Supported Platforms

- Node.js >= 18.0.0
- N-API version 8+
- Linux, macOS, Windows

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing`)
3. Build: `npm run build`
4. Test: `npm test`
5. Commit and push
6. Open a Pull Request

## Bug Reports

Found a bug? Please email **marino@flexbox.co.kr** with steps to reproduce.

## License

MIT
