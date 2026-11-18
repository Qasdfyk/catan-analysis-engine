import { describe, expect, it } from 'vitest';
import { Kind, chancesFrom, completePasses, luckError, planJobs, split, turnAt, type ChancePoint, type Counts } from './chances';

// A 2-player game: start, one turn (before and after its roll), the end.
const points: ChancePoint[] = [
  { state: 0, kind: Kind.Start, turn: 0, seat: 0 },
  { state: 10, kind: Kind.BeforeRoll, turn: 4, seat: 0 },
  { state: 11, kind: Kind.AfterRoll, turn: 4, seat: 0 },
  { state: 20, kind: Kind.End, turn: 4, seat: 0 },
];
const c = (a: number, b: number) => ({ wins: [a, b], unfinished: 0, playouts: a + b });

describe('win chances', () => {
  it('plans every point of a pass before the next pass', () => {
    expect(planJobs(2, 2)).toEqual([
      { point: 0, pass: 0 },
      { point: 1, pass: 0 },
      { point: 0, pass: 1 },
      { point: 1, pass: 1 },
    ]);
  });

  it('adds the playouts of all passes, and leaves points with none empty', () => {
    const counts: Counts = [[c(6, 4), c(5, 5)], [c(4, 6)]];
    expect(chancesFrom(counts, 3, 2)).toEqual([[0.5, 0.5], [0.5, 0.5], null]);
  });

  it('splits each change into luck (across a roll) and decisions, adding up to the end', () => {
    const chances = [
      [0.5, 0.5],
      [0.6, 0.4], // setup: decisions +0.1 for seat 0
      [0.3, 0.7], // the roll: luck -0.3
      [1, 0], // the rest: decisions +0.7
    ];
    const s = split(points, chances);
    expect(s.luck[0]).toBeCloseTo(-0.3);
    expect(s.decisions[0]).toBeCloseTo(0.8);
    for (let seat = 0; seat < 2; seat++) expect(s.start[seat] + s.luck[seat] + s.decisions[seat]).toBeCloseTo(s.end[seat]);
  });

  it('measures the noise of the split from complete passes only', () => {
    const pass = (rollWins: number) => [c(5, 5), c(5, 5), c(rollWins, 10 - rollWins), c(10, 0)];
    const counts: Counts = [pass(2), pass(4)];
    expect(completePasses(counts, 4)).toEqual([0, 1]);
    expect(luckError(points, counts, 2)).toBeNull(); // under 3 passes
    counts.push(pass(6), [c(1, 1)]); // the last pass is not complete
    expect(completePasses(counts, 4)).toEqual([0, 1, 2]);
    // Luck of seat 0 per pass: -0.3, -0.1, +0.1; sd 0.2, error 0.2 / sqrt(3).
    expect(luckError(points, counts, 2)![0]).toBeCloseTo(0.2 / Math.sqrt(3));
    // Results arrive out of order: a pass with only its last point is not complete.
    const sparse: Counts = [[]];
    sparse[0][3] = c(1, 1);
    expect(completePasses(sparse, 4)).toEqual([]);
  });

  it('finds the roll and the moves of the turn being shown', () => {
    const chances = [[0.5, 0.5], [0.6, 0.4], [0.3, 0.7], [1, 0]];
    expect(turnAt(points, chances, 5)).toBeNull(); // still the setup
    const t = turnAt(points, chances, 15)!;
    expect(t.turn).toBe(4);
    expect(t.roll[0]).toBeCloseTo(-0.3);
    expect(t.decisions![0]).toBeCloseTo(0.7);
  });
});
