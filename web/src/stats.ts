// Numbers about a finished game for the charts under the replay: victory
// points over time, the dice, and where each player's cards came from.
// Everything is read from the engine's states and the record's events, so it
// is what happened, not an estimate. Computed once per game; the charts then
// cut it at the shown position.

import type { CardArray, GameState, RecordEvent } from './types';

/** The part of OpenGame the statistics need (and tests can fake). */
export interface GameSource {
  record: { events: RecordEvent[]; players: unknown[] };
  stateCount: number;
  state(i: number): GameState;
}

export interface Roll {
  /** The state right after the roll. */
  pos: number;
  seat: number;
  sum: number;
  /** Cards each seat received from this roll, in RESOURCES order. */
  gains: CardArray[];
}

export interface Steal {
  pos: number;
  thief: number;
  victim: number;
}

export interface Discard {
  pos: number;
  seat: number;
  cards: number;
}

export interface GameStats {
  seats: number;
  /** points[i][seat]: all victory points, including hidden cards, in state i. */
  points: number[][];
  /** The state where each turn after the setup starts (its roll), by turn number. */
  turnStarts: { turn: number; pos: number }[];
  rolls: Roll[];
  steals: Steal[];
  discards: Discard[];
}

const zero = (): CardArray => [0, 0, 0, 0, 0];

export function gameStats(game: GameSource): GameStats {
  const seats = game.record.players.length;
  const points: number[][] = [];
  for (let i = 0; i < game.stateCount; i++) {
    points.push(game.state(i).players.map((p) => p.points + p.hidden_points));
  }

  const turnStarts: GameStats['turnStarts'] = [];
  const rolls: Roll[] = [];
  const steals: Steal[] = [];
  const discards: Discard[] = [];
  // Only events the engine accepted have a state after them.
  const events = game.record.events.slice(0, game.stateCount - 1);
  events.forEach((e, i) => {
    const pos = i + 1;
    switch (e.action) {
      case 'roll_dice': {
        const dice = (e.outcome?.dice as [number, number] | undefined) ?? [0, 0];
        const before = game.state(pos - 1);
        const after = game.state(pos);
        // A roll changes hands only by production: anything a seat gained here, the dice gave.
        const gains = after.players.map((p, seat) => p.resources.map((n, r) => Math.max(0, n - before.players[seat].resources[r])) as CardArray);
        rolls.push({ pos, seat: e.seat, sum: dice[0] + dice[1], gains });
        if (turnStarts.at(-1)?.turn !== e.turn) turnStarts.push({ turn: e.turn, pos });
        break;
      }
      case 'steal':
        if (typeof e.payload.victim_seat === 'number') steals.push({ pos, thief: e.seat, victim: e.payload.victim_seat });
        break;
      case 'discard': {
        const cards = Object.values((e.payload.cards as Record<string, number>) ?? {}).reduce((a, b) => a + b, 0);
        discards.push({ pos, seat: e.seat, cards });
        break;
      }
    }
  });
  return { seats, points, turnStarts, rolls, steals, discards };
}

/** Ways two dice make each sum from 2 to 12, out of 36. */
export const DICE_WAYS = [1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1];

/** How often each sum 2..12 came up among the rolls up to state `pos`. */
export function diceCounts(stats: GameStats, pos: number): number[] {
  const counts = DICE_WAYS.map(() => 0);
  for (const r of stats.rolls) if (r.pos <= pos && r.sum >= 2) counts[r.sum - 2]++;
  return counts;
}

export interface Income {
  /** Cards from rolls, by resource. */
  produced: CardArray;
  stolenFrom: number;
  stoleFromOthers: number;
  discarded: number;
}

/** Each seat's cards in and out up to state `pos`. */
export function income(stats: GameStats, pos: number): Income[] {
  const out: Income[] = Array.from({ length: stats.seats }, () => ({ produced: zero(), stolenFrom: 0, stoleFromOthers: 0, discarded: 0 }));
  for (const r of stats.rolls) {
    if (r.pos > pos) break;
    r.gains.forEach((g, seat) => g.forEach((n, i) => (out[seat].produced[i] += n)));
  }
  for (const s of stats.steals) {
    if (s.pos > pos) break;
    out[s.thief].stoleFromOthers++;
    out[s.victim].stolenFrom++;
  }
  for (const d of stats.discards) {
    if (d.pos > pos) break;
    out[d.seat].discarded += d.cards;
  }
  return out;
}

export const total = (cards: CardArray) => cards.reduce((a, b) => a + b, 0);
