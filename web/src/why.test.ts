// The words of the move review: moves as phrases, and "why" from the
// engine's facts, on the example game's board.

import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { beforeAll, describe, expect, it } from 'vitest';
import { loadEngine } from './engine';
import { byEvent, countGrades, reviewAt, worstMoves, type GradeRun, type MoveReview } from './grades';
import type { BoardInfo, GameRecord, Topology } from './types';
import { actionText, cornerText, hexText, whyText, type Names } from './why';

const text = gunzipSync(readFileSync(new URL('../public/example.json.gz', import.meta.url))).toString('utf8');
let names: Names;

beforeAll(async () => {
  const engine = await loadEngine();
  const replay = new engine.Replay(text);
  names = {
    players: (JSON.parse(text) as GameRecord).players,
    board: JSON.parse(replay.boardJson()) as BoardInfo,
    topo: JSON.parse(engine.topologyJson()) as Topology,
  };
  replay.delete();
});

const review = (over: Partial<MoveReview>): MoveReview => ({
  state: 10,
  seat: 2,
  turn: 3,
  played: { kind: 'end_turn' },
  best: { kind: 'end_turn' },
  played_value: 0.2,
  best_value: 0.2,
  loss: 0,
  error: 0.01,
  grade: 'best',
  within_noise: false,
  best_now: 0,
  played_now: 0,
  candidates: 4,
  playouts: 2000,
  why: null,
  ...over,
});

describe('move phrases', () => {
  it('names a corner by its numbers and a hex by number and terrain', () => {
    const land = names.topo.nodes.findIndex((n) => n.hexes.length === 3 && n.hexes.every((h) => names.board.hexes[h].number > 0));
    const numbers = names.topo.nodes[land].hexes.map((h) => names.board.hexes[h].number);
    expect(cornerText(names, land)).toBe(numbers.join('·'));
    const desert = names.board.hexes.findIndex((h) => h.terrain === 'desert');
    expect(hexText(names, desert)).toBe('the desert');
    const other = desert === 0 ? 1 : 0;
    expect(hexText(names, other)).toBe(`the ${names.board.hexes[other].number} ${names.board.hexes[other].terrain}`);
  });

  it('says every kind of move', () => {
    expect(actionText({ kind: 'upgrade_city', target: 0 }, names)).toMatch(/^a city on /);
    expect(actionText({ kind: 'steal', target: 1 }, names)).toBe('stealing from Ada');
    expect(actionText({ kind: 'trade_with_bank', first: 'wood', second: 'ore' }, names)).toBe('trading wood for ore with the bank');
    expect(actionText({ kind: 'offer_trade', give: { wood: 1 }, get: { ore: 1 } }, names)).toBe('offering 1 wood for 1 ore');
    expect(actionText({ kind: 'end_turn' }, names)).toBe('ending the turn');
  });
});

describe('why', () => {
  it('is silent for best and good', () => {
    expect(whyText(review({ grade: 'good', why: null }), names)).toBeNull();
  });

  it('names a win on the spot', () => {
    const r = review({ grade: 'blunder', best: { kind: 'upgrade_city', target: 0 }, why: { kind: 'wins_now', best_now: 1, played_now: 0 } });
    expect(whyText(r, names)).toMatch(/^A city on .* wins the game this turn in 100% of the playouts; your move cannot win it this turn\.$/);
  });

  it('says whose pips the robber takes, and never claims what the pips do not show', () => {
    const leader = review({
      grade: 'mistake',
      best: { kind: 'move_robber', target: 5 },
      why: { kind: 'robber', best_hex: 5, played_hex: 18, best_blocks: [0, 8, 0, 0], played_blocks: [0, 3, 0, 0], leader: 1 },
    });
    expect(whyText(leader, names)).toContain('takes 8 pips from Ada, the leader; yours took 3.');
    const worseByPips = review({
      grade: 'inaccuracy',
      best: { kind: 'move_robber', target: 5 },
      why: { kind: 'robber', best_hex: 5, played_hex: 18, best_blocks: [0, 0, 6, 3], played_blocks: [0, 10, 0, 10], leader: 1 },
    });
    expect(whyText(worseByPips, names)).toMatch(/^By pips your hex was no worse: it blocked 20 of your opponents' pips, .* would block 3\./);
  });

  it('lists what a trade pays for', () => {
    const r = review({
      grade: 'mistake',
      best: { kind: 'trade_with_bank', first: 'brick', second: 'sheep' },
      why: { kind: 'enables', builds: ['city', 'development card'] },
    });
    expect(whyText(r, names)).toBe('After trading brick for sheep with the bank you can pay for a city or a development card; after your move you cannot.');
  });
});

describe('grades on the page', () => {
  const run: GradeRun = {
    seat: 2,
    decisions: [10, 20, 30].map((state) => ({ state, turn: 1, kind: 'x' })),
    reviews: [review({ state: 10 }), review({ state: 20, grade: 'blunder', loss: 0.3 }), review({ state: 30, grade: 'inaccuracy', loss: 0.04 })],
    workers: 1,
    seconds: 1,
    error: '',
  };

  it('counts best and good together, and lists the worst moves first', () => {
    expect(countGrades(run)).toEqual({ good: 1, inaccuracy: 1, mistake: 0, blunder: 1 });
    expect(worstMoves(run).map((r) => r.state)).toEqual([20, 30]);
  });

  it('finds the move just made, else the one about to be made', () => {
    expect(reviewAt(run, 21)).toEqual({ review: run.reviews[1], made: true });
    expect(reviewAt(run, 20)).toEqual({ review: run.reviews[1], made: false });
    expect(reviewAt(run, 25)).toBeNull();
    expect([...byEvent(run).keys()]).toEqual([10, 20, 30]);
  });
});
