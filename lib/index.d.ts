export interface SearchOptions {
    overlap?: boolean;
    offset?: number;
    limit?: number;
}

export function search(haystack: Buffer, needle: Buffer | string, options?: SearchOptions): number[];
export function searchAsync(haystack: Buffer, needle: Buffer | string, options?: SearchOptions): Promise<number[]>;
export function count(haystack: Buffer, needle: Buffer | string, options?: SearchOptions): number;

import { Transform, TransformOptions } from 'stream';

export interface SearchStreamOptions extends TransformOptions {
    overlap?: boolean;
}

export function createSearchStream(needle: Buffer | string, options?: SearchStreamOptions): Transform;
