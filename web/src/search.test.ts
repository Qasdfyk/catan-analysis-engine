import { describe, expect, it } from 'vitest';
import { mergeParts, searchChecksum, type RootMove } from './search';

const move = (name: string, visits: number, wins: number): RootMove => ({ move: name, kind: name, target: -1, prior: 0.5, visits, wins });

describe('mergeParts', () => {
  it('adds the same move up and keeps the order moves were first seen', () => {
    const merged = mergeParts([
      { moves: [move('end_turn', 3, 1), move('buy_dev_card', 2, 0)] },
      { moves: [move('build_road 4', 1, 1), move('end_turn', 4, 2)] },
    ]);
    expect(merged.map((m) => m.move)).toEqual(['end_turn', 'buy_dev_card', 'build_road 4']);
    expect(merged[0]).toMatchObject({ visits: 7, wins: 3 });
  });
  it('does not change the parts', () => {
    const part = { moves: [move('end_turn', 3, 1)] };
    mergeParts([part, part]);
    expect(part.moves[0].visits).toBe(3);
  });
});

describe('searchChecksum', () => {
  it('folds like catan_mcts: (visits * 1000 + wins) * (position + 1) * (decision + 1)', () => {
    // (3*1000+1)*1*6 + (2*1000+0)*2*6 = 18006 + 24000
    expect(searchChecksum([move('a', 3, 1), move('b', 2, 0)], 5)).toBe('42006');
  });
});
