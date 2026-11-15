// Statistics of the example game, replayed by the real WebAssembly module.

import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { afterAll, describe, expect, it } from 'vitest';
import { OpenGame, loadEngine } from './engine';
import { diceCounts, gameStats, income, total } from './stats';

const text = gunzipSync(readFileSync(new URL('../public/example.json.gz', import.meta.url))).toString('utf8');
const game = new OpenGame(await loadEngine(), text);
const stats = gameStats(game);
const last = game.stateCount - 1;
afterAll(() => game.close());

describe('gameStats on the example game', () => {
  it('ends with the recorded victory points', () => {
    expect(stats.points[last]).toEqual(game.record.result!.victory_points);
    expect(stats.points[0]).toEqual([0, 0, 0, 0]);
  });

  it('counts every roll once', () => {
    const rolls = game.record.events.filter((e) => e.action === 'roll_dice').length;
    expect(diceCounts(stats, last).reduce((a, b) => a + b, 0)).toBe(rolls);
    expect(diceCounts(stats, 0).every((n) => n === 0)).toBe(true);
  });

  it('gives no cards on a seven', () => {
    for (const r of stats.rolls.filter((r) => r.sum === 7)) {
      expect(r.gains.map(total)).toEqual([0, 0, 0, 0]);
    }
  });

  it('matches the cards the bank paid out on each roll', () => {
    for (const r of stats.rolls) {
      const bankBefore = game.state(r.pos - 1).bank;
      const bankAfter = game.state(r.pos).bank;
      const paid = bankBefore.reduce((a, n, i) => a + n - bankAfter[i], 0);
      expect(r.gains.reduce((a, g) => a + total(g), 0)).toBe(paid);
    }
  });

  it('pairs every steal with a victim', () => {
    const all = income(stats, last);
    const stolen = all.reduce((a, x) => a + x.stolenFrom, 0);
    expect(stolen).toBe(all.reduce((a, x) => a + x.stoleFromOthers, 0));
    expect(stolen).toBe(game.record.events.filter((e) => e.action === 'steal').length);
  });

  it('marks the start of every turn after the setup', () => {
    for (const t of stats.turnStarts) expect(game.record.events[t.pos - 1].action).toBe('roll_dice');
    expect(stats.turnStarts.map((t) => t.turn)).toEqual([...stats.turnStarts.map((t) => t.turn)].sort((a, b) => a - b));
  });
});
