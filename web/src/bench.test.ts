import { describe, expect, it } from 'vitest';
import { workerCounts } from './bench';

describe('workerCounts', () => {
  it('doubles up to the core count and ends on it', () => {
    expect(workerCounts(16)).toEqual([1, 2, 4, 8, 16]);
    expect(workerCounts(12)).toEqual([1, 2, 4, 8, 12]);
    expect(workerCounts(1)).toEqual([1]);
  });
  it('stops at 32 workers', () => {
    expect(workerCounts(128).at(-1)).toBe(32);
  });
});
