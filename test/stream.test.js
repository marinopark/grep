'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { Readable } = require('stream');
const grep = require('../lib');

function collectMatches(stream) {
    return new Promise((resolve, reject) => {
        const matches = [];
        stream.on('match', (offset) => matches.push(offset));
        stream.on('finish', () => resolve(matches));
        stream.on('error', reject);
    });
}

function createChunkedReadable(chunks) {
    let i = 0;
    return new Readable({
        read() {
            if (i < chunks.length) {
                this.push(chunks[i++]);
            } else {
                this.push(null);
            }
        }
    });
}

describe('createSearchStream', () => {
    it('finds pattern within a single chunk', async () => {
        const stream = grep.createSearchStream('hello');
        const src = createChunkedReadable([Buffer.from('say hello world')]);
        const matches = collectMatches(stream);
        src.pipe(stream);
        assert.deepStrictEqual(await matches, [4]);
    });

    it('finds pattern spanning chunk boundary', async () => {
        const stream = grep.createSearchStream('hello');
        /* 'hel' in first chunk, 'lo' in second */
        const src = createChunkedReadable([
            Buffer.from('say hel'),
            Buffer.from('lo world')
        ]);
        const matches = collectMatches(stream);
        src.pipe(stream);
        assert.deepStrictEqual(await matches, [4]);
    });

    it('finds multiple matches across chunks', async () => {
        const stream = grep.createSearchStream('ab');
        const src = createChunkedReadable([
            Buffer.from('xab'),
            Buffer.from('yab'),
            Buffer.from('z')
        ]);
        const matches = collectMatches(stream);
        src.pipe(stream);
        assert.deepStrictEqual(await matches, [1, 4]);
    });

    it('overlap option works in stream', async () => {
        const stream = grep.createSearchStream('aa', { overlap: true });
        const src = createChunkedReadable([Buffer.from('aaaa')]);
        const matches = collectMatches(stream);
        src.pipe(stream);
        assert.deepStrictEqual(await matches, [0, 1, 2]);
    });

    it('passes data through (transform)', async () => {
        const stream = grep.createSearchStream('x');
        const src = createChunkedReadable([Buffer.from('hello')]);
        const chunks = [];
        stream.on('data', (c) => chunks.push(c));
        const matches = collectMatches(stream);
        src.pipe(stream);
        await matches;
        assert.deepStrictEqual(Buffer.concat(chunks).toString(), 'hello');
    });

    it('handles binary pattern across boundary', async () => {
        const needle = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);
        const stream = grep.createSearchStream(needle);
        /* Split in the middle of the pattern */
        const src = createChunkedReadable([
            Buffer.from([0x00, 0x00, 0xDE, 0xAD]),
            Buffer.from([0xBE, 0xEF, 0x00, 0x00])
        ]);
        const matches = collectMatches(stream);
        src.pipe(stream);
        assert.deepStrictEqual(await matches, [2]);
    });

    it('throws on empty needle', () => {
        assert.throws(
            () => grep.createSearchStream(''),
            /needle must not be empty/
        );
    });
});
