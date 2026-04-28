'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const grep = require('../lib');

describe('edge cases', () => {
    it('throws on empty needle (search)', () => {
        assert.throws(
            () => grep.search(Buffer.from('test'), ''),
            /needle must not be empty/
        );
    });

    it('throws on empty needle (count)', () => {
        assert.throws(
            () => grep.count(Buffer.from('test'), Buffer.alloc(0)),
            /needle must not be empty/
        );
    });

    it('throws on empty needle (searchAsync)', async () => {
        await assert.rejects(
            grep.searchAsync(Buffer.from('test'), ''),
            /needle must not be empty/
        );
    });

    it('throws on non-Buffer haystack (search)', () => {
        assert.throws(
            () => grep.search('not a buffer', 'test'),
            /haystack must be a Buffer/
        );
    });

    it('throws on non-Buffer haystack (count)', () => {
        assert.throws(
            () => grep.count('not a buffer', 'test'),
            /haystack must be a Buffer/
        );
    });

    it('returns empty when needle longer than haystack', () => {
        const hay = Buffer.from('ab');
        assert.deepStrictEqual(grep.search(hay, 'abcdef'), []);
    });

    it('returns 0 count when needle longer than haystack', () => {
        const hay = Buffer.from('ab');
        assert.strictEqual(grep.count(hay, 'abcdef'), 0);
    });

    it('handles 1-byte haystack with match', () => {
        assert.deepStrictEqual(grep.search(Buffer.from('x'), 'x'), [0]);
    });

    it('handles 1-byte haystack without match', () => {
        assert.deepStrictEqual(grep.search(Buffer.from('x'), 'y'), []);
    });

    it('offset beyond haystack length returns empty', () => {
        const hay = Buffer.from('hello');
        assert.deepStrictEqual(grep.search(hay, 'h', { offset: 100 }), []);
    });

    it('offset at exact last possible position', () => {
        const hay = Buffer.from('abcabc');
        assert.deepStrictEqual(grep.search(hay, 'abc', { offset: 3 }), [3]);
    });

    it('limit of 0 behaves like no limit', () => {
        /* 0 and negative = no limit */
        const hay = Buffer.from('aaa');
        const result = grep.search(hay, 'a', { limit: 0 });
        assert.deepStrictEqual(result, [0, 1, 2]);
    });

    it('works with long pattern (Boyer-Moore path)', () => {
        const pattern = 'abcdefghijklmnop'; /* 16 bytes, > BM_LINEAR_THRESHOLD */
        const hay = Buffer.from('xxxxx' + pattern + 'yyyyy' + pattern + 'zzzzz');
        assert.deepStrictEqual(grep.search(hay, pattern), [5, 26]);
    });

    it('works with pattern at threshold boundary (4 bytes)', () => {
        const hay = Buffer.from('xxABCDyyABCDzz');
        assert.deepStrictEqual(grep.search(hay, 'ABCD'), [2, 8]);
    });

    it('works with pattern just above threshold (5 bytes)', () => {
        const hay = Buffer.from('xxABCDEyyABCDEzz');
        assert.deepStrictEqual(grep.search(hay, 'ABCDE'), [2, 9]);
    });

    it('handles large buffer', () => {
        const size = 1024 * 1024; /* 1MB */
        const hay = Buffer.alloc(size, 0x41); /* all 'A' */
        hay.write('NEEDLE', size - 6);
        assert.deepStrictEqual(grep.search(hay, 'NEEDLE'), [size - 6]);
    });
});
