'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const grep = require('../lib');

describe('binary pattern search', () => {
    it('finds 0xDEADBEEF pattern', () => {
        const needle = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);
        const hay = Buffer.alloc(100, 0x00);
        needle.copy(hay, 20);
        needle.copy(hay, 60);
        assert.deepStrictEqual(grep.search(hay, needle), [20, 60]);
    });

    it('finds pattern containing 0x00 bytes', () => {
        const needle = Buffer.from([0x00, 0x01, 0x00]);
        const hay = Buffer.from([
            0xFF, 0x00, 0x01, 0x00, 0xFF, 0xFF,
            0x00, 0x01, 0x00, 0xFF
        ]);
        assert.deepStrictEqual(grep.search(hay, needle), [1, 6]);
    });

    it('finds 0xFF bytes', () => {
        const needle = Buffer.from([0xFF, 0xFF]);
        const hay = Buffer.from([0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF]);
        assert.deepStrictEqual(grep.search(hay, needle), [1, 4]);
    });

    it('finds pattern in all-zero buffer', () => {
        const hay = Buffer.alloc(256, 0x00);
        const needle = Buffer.from([0x00, 0x00, 0x00]);
        /* Without overlap, step by 3 */
        const expected = [];
        for (let i = 0; i <= 253; i += 3) expected.push(i);
        assert.deepStrictEqual(grep.search(hay, needle), expected);
    });

    it('counts binary pattern occurrences', () => {
        const needle = Buffer.from([0xCA, 0xFE]);
        const hay = Buffer.alloc(50, 0x00);
        hay[10] = 0xCA; hay[11] = 0xFE;
        hay[30] = 0xCA; hay[31] = 0xFE;
        assert.strictEqual(grep.count(hay, needle), 2);
    });

    it('handles mixed binary and ASCII', () => {
        const hay = Buffer.from([
            0x48, 0x65, 0x6C, 0x6C, 0x6F, // "Hello"
            0x00, 0xFF, 0x00,               // binary
            0x57, 0x6F, 0x72, 0x6C, 0x64   // "World"
        ]);
        assert.deepStrictEqual(grep.search(hay, Buffer.from([0x00, 0xFF, 0x00])), [5]);
        assert.deepStrictEqual(grep.search(hay, 'World'), [8]);
    });
});
