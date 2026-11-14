import { describe, expect, it } from 'vitest';
import { changesBetween } from './board';
import type { GameState, PlayerState } from './types';

const player = (p: Partial<PlayerState>): PlayerState => ({
  resources: [0, 0, 0, 0, 0],
  points: 0,
  hidden_points: 0,
  dev_cards: [0, 0, 0, 0, 0],
  new_dev_cards: [0, 0, 0, 0, 0],
  knights: 0,
  settlements: [],
  cities: [],
  roads: [],
  ...p,
});

const state = (players: PlayerState[], robber = 9): GameState =>
  ({ players, robber }) as unknown as GameState;

describe('changesBetween', () => {
  it('marks a new settlement and road', () => {
    const c = changesBetween(state([player({})]), state([player({ settlements: [3], roads: [5] })]));
    expect([...c.nodes]).toEqual([3]);
    expect([...c.edges]).toEqual([5]);
    expect(c.robberMoved).toBe(false);
  });

  it('marks a settlement upgraded to a city', () => {
    const c = changesBetween(state([player({ settlements: [3] })]), state([player({ cities: [3] })]));
    expect([...c.nodes]).toEqual([3]);
  });

  it('marks a moved robber', () => {
    expect(changesBetween(state([player({})], 9), state([player({})], 4)).robberMoved).toBe(true);
  });

  it('marks nothing at the start', () => {
    const c = changesBetween(null, state([player({ settlements: [1] })]));
    expect(c.nodes.size + c.edges.size).toBe(0);
  });
});
