// The example review shipped with the start page must be what the Replay tab
// computes live for the same game: same settings, and a sample of points
// played again here gives the same counts.

import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import { PASSES, PASS_SIZE, SEED } from './chances';
import { loadEngine } from './engine';
import { fromFile, type ExampleReviewFile } from './review';
import { BUDGET, SETUP_SEED } from './setup';

const file = JSON.parse(readFileSync(new URL('../public/example-review.json', import.meta.url), 'utf8')) as ExampleReviewFile;
const text = gunzipSync(readFileSync(new URL(`../public/${file.record}`, import.meta.url))).toString('utf8');

describe('example review', () => {
  it('was computed with the settings of the live review', () => {
    expect(file.record).toBe('example.json.gz');
    expect(file.settings).toEqual({ passes: PASSES, passSize: PASS_SIZE, seed: SEED, setupSeed: SETUP_SEED, budget: BUDGET });
  });

  it('is complete', () => {
    const { chances, setup } = fromFile(file);
    expect(chances.counts).toHaveLength(PASSES);
    for (const pass of chances.counts) for (let i = 0; i < chances.points.length; i++) expect(pass[i]?.playouts).toBe(PASS_SIZE);
    expect(setup.reviews).toHaveLength(setup.moves.length);
    expect(setup.reviews.every((r) => r?.done)).toBe(true);
  });

  it('matches the engine on a sample of points', async () => {
    const engine = await loadEngine();
    const replay = new engine.Replay(text);
    try {
      const points = JSON.parse(replay.chancePointsJson());
      expect(points).toEqual(file.chances.points);
      for (const [point, pass] of [[0, 0], [40, 3], [points.length - 2, 9]]) {
        const counts = JSON.parse(replay.winChances(points[point].state, pass * PASS_SIZE, PASS_SIZE, SEED));
        expect(counts).toEqual(file.chances.counts[pass][point]);
      }
      expect(JSON.parse(replay.setupMovesJson())).toEqual(file.setup.moves);
    } finally {
      replay.delete();
    }
  });
});
