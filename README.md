# @marinopark/grep

Native C Boyer-Moore binary pattern search for Node.js Buffers. grep-level performance without leaving Node.

## Why?

The existing npm ecosystem lacks a package that combines **N-API C** with **raw Buffer** support using **Boyer-Moore** search:

| Package | N-API C | Buffer support | Algorithm |
|---------|---------|----------------|-----------|
| `fast-string-search` | Yes | No (UTF-16 only) | Boyer-Moore |
| `bop` | No (pure JS) | Yes | Boyer-Moore |
| `streamsearch` | No (pure JS) | Yes (stream) | Boyer-Moore-Horspool |
| **`@marinopark/grep`** | **Yes** | **Yes (zero-copy)** | **Boyer-Moore (full)** |

This package runs Boyer-Moore directly on `uint8_t*` data from Node.js Buffers via N-API — no string conversion, no copying.

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

This package implements the full **Boyer-Moore** string search algorithm with both the **Bad Character Rule** and the **Good Suffix Rule**:

- **Bad Character Rule**: A 256-entry shift table (full byte alphabet) allows skipping ahead when a mismatch occurs.
- **Good Suffix Rule**: A suffix shift table, precomputed from the pattern, provides additional skip distance.
- **Best case**: O(n/m) — sublinear, skips large portions of the haystack.
- **Worst case**: O(n) — linear scan (with Galil optimization consideration).

For patterns of 4 bytes or shorter, the overhead of Boyer-Moore preprocessing exceeds its benefit, so the implementation falls back to a simple linear scan.

## Benchmarks

Run benchmarks with:

```bash
npm run bench
```

| Scenario | Buffer.indexOf loop | grep.search | grep.searchAsync |
|----------|-------------------|-------------|-----------------|
| 1KB, 4B pattern | 0.002ms | 0.004ms | 0.017ms |
| 1MB, 16B pattern | 0.063ms | 0.465ms | 0.489ms |
| 100MB, 64B pattern | 2.842ms | 31.904ms | 32.054ms |
| Worst case (1MB, 0x00) | 49.765ms | 57.250ms | 58.171ms |
| No match (1MB, 16B) | 0.028ms | 0.239ms | 0.291ms |

*(Measured on Windows 11, Node.js 22, AMD64. Run `npm run bench` on your machine for your own results.)*

> **Note:** V8's `Buffer.indexOf` uses highly optimized platform-specific SIMD instructions internally, making it extremely fast for single-occurrence lookup. The primary value of `@marinopark/grep` is the **all-at-once API** (find every occurrence in a single call), **streaming support** with cross-boundary matching, and **async execution** on a worker thread — not raw per-byte throughput over V8's built-in.

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

## License

MIT
