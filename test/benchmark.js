'use strict';

const { performance } = require('perf_hooks');
const grep = require('../lib');

function bench(name, fn, iterations = 10) {
    /* Warmup */
    for (let i = 0; i < 3; i++) fn();

    const times = [];
    for (let i = 0; i < iterations; i++) {
        const start = performance.now();
        fn();
        times.push(performance.now() - start);
    }
    times.sort((a, b) => a - b);
    const median = times[Math.floor(times.length / 2)];
    const min = times[0];
    const max = times[times.length - 1];
    console.log(`  ${name}: median=${median.toFixed(3)}ms  min=${min.toFixed(3)}ms  max=${max.toFixed(3)}ms`);
    return median;
}

async function benchAsync(name, fn, iterations = 10) {
    /* Warmup */
    for (let i = 0; i < 3; i++) await fn();

    const times = [];
    for (let i = 0; i < iterations; i++) {
        const start = performance.now();
        await fn();
        times.push(performance.now() - start);
    }
    times.sort((a, b) => a - b);
    const median = times[Math.floor(times.length / 2)];
    const min = times[0];
    const max = times[times.length - 1];
    console.log(`  ${name}: median=${median.toFixed(3)}ms  min=${min.toFixed(3)}ms  max=${max.toFixed(3)}ms`);
    return median;
}

function bufferIndexOfAll(haystack, needle) {
    const results = [];
    let pos = 0;
    while (pos < haystack.length) {
        const idx = haystack.indexOf(needle, pos);
        if (idx === -1) break;
        results.push(idx);
        pos = idx + needle.length;
    }
    return results;
}

function makeHaystack(size, patternBuf, insertEvery) {
    const hay = Buffer.alloc(size, 0x41); /* fill with 'A' */
    if (insertEvery > 0) {
        for (let i = 0; i + patternBuf.length <= size; i += insertEvery) {
            patternBuf.copy(hay, i);
        }
    }
    return hay;
}

async function run() {
    console.log('=== @marinopark/grep Benchmark ===\n');

    /* Scenario 1: Small haystack (1KB), short pattern (4 bytes) */
    console.log('1. Small haystack (1KB), 4-byte pattern');
    const hay1 = makeHaystack(1024, Buffer.from('FIND'), 200);
    const pat1 = Buffer.from('FIND');
    bench('Buffer.indexOf loop', () => bufferIndexOfAll(hay1, pat1));
    bench('grep.search', () => grep.search(hay1, pat1));
    await benchAsync('grep.searchAsync', () => grep.searchAsync(hay1, pat1));
    console.log();

    /* Scenario 2: Medium haystack (1MB), 16-byte pattern */
    console.log('2. Medium haystack (1MB), 16-byte pattern');
    const pat2 = Buffer.from('SEARCHPATTERN16!');
    const hay2 = makeHaystack(1024 * 1024, pat2, 4096);
    bench('Buffer.indexOf loop', () => bufferIndexOfAll(hay2, pat2));
    bench('grep.search', () => grep.search(hay2, pat2));
    await benchAsync('grep.searchAsync', () => grep.searchAsync(hay2, pat2));
    console.log();

    /* Scenario 3: Large haystack (100MB), 64-byte pattern */
    console.log('3. Large haystack (100MB), 64-byte pattern');
    const pat3 = Buffer.alloc(64);
    pat3.write('THIS_IS_A_64_BYTE_PATTERN_FOR_BENCHMARKING_BINARY_SEARCH_SPEED!');
    const hay3 = makeHaystack(100 * 1024 * 1024, pat3, 1024 * 1024);
    bench('Buffer.indexOf loop', () => bufferIndexOfAll(hay3, pat3), 5);
    bench('grep.search', () => grep.search(hay3, pat3), 5);
    await benchAsync('grep.searchAsync', () => grep.searchAsync(hay3, pat3), 5);
    console.log();

    /* Scenario 4: Worst case – very frequent pattern */
    console.log('4. Worst case: frequent 0x00 pattern in 1MB buffer');
    const hay4 = Buffer.alloc(1024 * 1024, 0x00);
    const pat4 = Buffer.from([0x00]);
    bench('Buffer.indexOf loop', () => bufferIndexOfAll(hay4, pat4), 5);
    bench('grep.search', () => grep.search(hay4, pat4), 5);
    await benchAsync('grep.searchAsync', () => grep.searchAsync(hay4, pat4), 5);
    console.log();

    /* Scenario 5: No match */
    console.log('5. No match: 1MB haystack, non-existent 16-byte pattern');
    const hay5 = Buffer.alloc(1024 * 1024, 0x41);
    const pat5 = Buffer.from('ZZZZZZZZZZZZZZZZ');
    bench('Buffer.indexOf loop', () => bufferIndexOfAll(hay5, pat5));
    bench('grep.search', () => grep.search(hay5, pat5));
    await benchAsync('grep.searchAsync', () => grep.searchAsync(hay5, pat5));
    console.log();

    console.log('=== Done ===');
}

run().catch(console.error);
