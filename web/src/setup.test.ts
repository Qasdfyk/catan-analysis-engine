import { describe, expect, it } from 'vitest';
import { bestValue, focusMove, heatColor, reviewOrder, verdict, type Review, type SetupMove } from './setup';

const moves: SetupMove[] = [
  { state: 0, seat: 0, node: 10, round: 0 },
  { state: 2, seat: 1, node: 20, round: 0 },
  { state: 4, seat: 1, node: 30, round: 1 },
];

const review = (over: Partial<Review>): Review => ({
  done: true,
  rounds: 5,
  playouts: 1000,
  seat: 0,
  played: 0,
  best: 1,
  candidates: [
    { node: 10, value: 0.2, playouts: 1600, dropped: false, finalist: true },
    { node: 11, value: 0.3, playouts: 1600, dropped: false, finalist: true },
    { node: 12, value: 0.5, playouts: 100, dropped: true, finalist: false },
  ],
  loss: { loss: 0.1, error: 0.02 },
  ...over,
});

describe('focusMove', () => {
  it('shows a settlement before it is placed and until its road', () => {
    expect(focusMove(moves, 0)).toBe(0);
    expect(focusMove(moves, 1)).toBe(0);
    expect(focusMove(moves, 2)).toBe(1);
    expect(focusMove(moves, 5)).toBe(2);
  });
  it('shows nothing after the setup', () => {
    expect(focusMove(moves, 6)).toBeNull();
  });
});

describe('bestValue', () => {
  it('is the confirmed best once done, even if a dropped corner looked higher', () => {
    expect(bestValue(review({}))).toBe(0.3);
  });
  it('is the top corner still in while the halving runs', () => {
    expect(bestValue(review({ done: false, best: -1 }))).toBe(0.3);
  });
});

describe('verdict', () => {
  it('grades the corner played', () => {
    expect(verdict(review({ played: 1 }))).toBe('best');
    expect(verdict(review({}))).toBe('loss');
    expect(verdict(review({ loss: { loss: 0.03, error: 0.02 } }))).toBe('noise');
  });
  it('waits for the confirmation', () => {
    expect(verdict(review({ done: false, loss: undefined }))).toBeNull();
  });
});

describe('heatColor', () => {
  it('goes from green to red over 15 points', () => {
    expect(heatColor(0)).toBe('hsl(120 70% 50%)');
    expect(heatColor(0.15)).toBe('hsl(0 70% 50%)');
    expect(heatColor(1)).toBe('hsl(0 70% 50%)');
  });
});

describe('reviewOrder', () => {
  it("reviews the seat's own settlements first", () => {
    const moves = [0, 1, 2, 2, 1, 0].map((seat, i) => ({ state: i, seat, node: i, round: 0 }));
    expect(reviewOrder(moves, 2)).toEqual([2, 3, 0, 1, 4, 5]);
    expect(reviewOrder(moves, 0)).toEqual([0, 5, 1, 2, 3, 4]);
  });
});
