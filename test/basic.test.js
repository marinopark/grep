'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const grep = require('../lib');

describe('search – basic', () => {
    it('finds ASCII pattern in buffer', () => {
        const hay = Buffer.from('the quick brown fox jumps over the lazy dog');
        assert.deepStrictEqual(grep.search(hay, 'fox'), [16]);
    });

    it('finds multiple occurrences', () => {
        const hay = Buffer.from('abcabcabc');
        assert.deepStrictEqual(grep.search(hay, 'abc'), [0, 3, 6]);
    });

    it('finds pattern at start', () => {
        const hay = Buffer.from('hello world');
        assert.deepStrictEqual(grep.search(hay, 'hello'), [0]);
    });

    it('finds pattern at end', () => {
        const hay = Buffer.from('hello world');
        assert.deepStrictEqual(grep.search(hay, 'world'), [6]);
    });

    it('returns empty array when no match', () => {
        const hay = Buffer.from('hello world');
        assert.deepStrictEqual(grep.search(hay, 'xyz'), []);
    });

    it('works with Buffer needle', () => {
        const hay = Buffer.from('abcdef');
        assert.deepStrictEqual(grep.search(hay, Buffer.from('cde')), [2]);
    });

    it('haystack equals needle (exact match)', () => {
        const buf = Buffer.from('exactmatch');
        assert.deepStrictEqual(grep.search(buf, 'exactmatch'), [0]);
    });

    it('single byte pattern', () => {
        const hay = Buffer.from('abacada');
        assert.deepStrictEqual(grep.search(hay, 'a'), [0, 2, 4, 6]);
    });
});

describe('search – options', () => {
    it('overlap: false (default) skips overlapping matches', () => {
        const hay = Buffer.from('aaaa');
        assert.deepStrictEqual(grep.search(hay, 'aa'), [0, 2]);
    });

    it('overlap: true includes overlapping matches', () => {
        const hay = Buffer.from('aaaa');
        assert.deepStrictEqual(grep.search(hay, 'aa', { overlap: true }), [0, 1, 2]);
    });

    it('offset skips initial bytes', () => {
        const hay = Buffer.from('abcabc');
        assert.deepStrictEqual(grep.search(hay, 'abc', { offset: 1 }), [3]);
    });

    it('limit caps result count', () => {
        const hay = Buffer.from('aaaaaa');
        assert.deepStrictEqual(grep.search(hay, 'a', { limit: 3 }), [0, 1, 2]);
    });

    it('offset + limit combined', () => {
        const hay = Buffer.from('abababab');
        assert.deepStrictEqual(
            grep.search(hay, 'ab', { offset: 2, limit: 2 }),
            [2, 4]
        );
    });
});

describe('count', () => {
    it('counts occurrences', () => {
        const hay = Buffer.from('abcabcabc');
        assert.strictEqual(grep.count(hay, 'abc'), 3);
    });

    it('counts with overlap', () => {
        const hay = Buffer.from('aaaa');
        assert.strictEqual(grep.count(hay, 'aa', { overlap: true }), 3);
    });

    it('returns 0 for no match', () => {
        const hay = Buffer.from('hello');
        assert.strictEqual(grep.count(hay, 'xyz'), 0);
    });
});

describe('searchAsync', () => {
    it('returns a Promise', () => {
        const hay = Buffer.from('test');
        const result = grep.searchAsync(hay, 'test');
        assert.ok(result instanceof Promise);
    });

    it('resolves with same results as search', async () => {
        const hay = Buffer.from('abcXabcXabc');
        const sync = grep.search(hay, 'abc');
        const async_ = await grep.searchAsync(hay, 'abc');
        assert.deepStrictEqual(async_, sync);
    });

    it('respects options', async () => {
        const hay = Buffer.from('aabaa');
        const result = await grep.searchAsync(hay, 'a', { limit: 2 });
        assert.deepStrictEqual(result, [0, 1]);
    });
});
