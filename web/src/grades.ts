// The move review on the page: every decision of the player the review is
// for, graded in Web Workers (grades-worker.ts) by the engine's review
// (include/catan/review.hpp): the chance to win after the best move and
// after the move played, the loss between them with its error, a grade, and
// the facts behind "why" (why.ts puts them into words).
//
// Each worker grades whole decisions, one at a time, in the order of the
// game, so the move list fills in from the first turn on. A player's grades
// are kept while the page stays on the game: picking another player and
// back does not start over.

import { useEffect, useState } from 'react';
import { workerCount } from './chances';

/** catan_review's defaults: 32 playouts a move to start, 2 finalists, 800 to confirm. */
export const BUDGET = { first: 32, finalists: 2, confirm: 800 } as const;
export const REVIEW_SEED = 1;

export type Grade = 'best' | 'good' | 'inaccuracy' | 'mistake' | 'blunder';

/** A move as action_json writes it (record.cpp). */
export interface ActionJson {
  kind: string;
  target?: number;
  first?: string;
  second?: string;
  give?: Record<string, number>;
  get?: Record<string, number>;
}

/** explain() of review.hpp: the facts behind "why", for an inaccuracy or worse. */
export type Why =
  | { kind: 'wins_now'; best_now: number; played_now: number }
  | { kind: 'robber'; best_hex: number; played_hex: number; best_blocks: number[]; played_blocks: number[]; leader: number }
  | { kind: 'victim'; best_victim: number; played_victim: number; points: number[]; cards: number[] }
  | { kind: 'longest_road' }
  | { kind: 'largest_army' }
  | { kind: 'points'; best_points: number; played_points: number }
  | { kind: 'production'; node: number; city: boolean; pips: number; numbers: number[]; played_pips: number }
  | { kind: 'port'; node: number; port: string }
  | { kind: 'enables'; builds: string[] }
  | { kind: 'road_spot'; node: number; pips: number; numbers: number[] }
  | { kind: 'road_length'; best_length: number; played_length: number; holder: number; holder_length: number }
  | { kind: 'ended_turn' }
  | { kind: 'dev_card'; unseen: number; points: number; knights: number; my_knights: number; holder: number; holder_knights: number }
  | { kind: 'later' };

/** review_json() of review.hpp plus "why". Values are chances to win, 0..1. */
export interface MoveReview {
  /** State before the move; the move is event `state` of the record. */
  state: number;
  seat: number;
  turn: number;
  played: ActionJson;
  best: ActionJson;
  played_value: number;
  best_value: number;
  /** best_value − played_value; 0 when the move played is the best. */
  loss: number;
  /** Standard error of the loss, from pairs of playouts. */
  error: number;
  grade: Grade;
  within_noise: boolean;
  best_now: number;
  played_now: number;
  candidates: number;
  playouts: number;
  why: Why | null;
}

/** reviewDecisionsJson(): the decisions the review grades. */
export interface Decision {
  state: number;
  turn: number;
  kind: string;
}

export interface GradeRun {
  seat: number;
  decisions: Decision[];
  /** reviews[k] of decisions[k], as they come in. */
  reviews: (MoveReview | undefined)[];
  workers: number;
  seconds: number;
  error: string;
  /** Computed ahead of time (the start page's example), not in this browser. */
  precomputed?: boolean;
}

/**
 * Best and good are shown as one grade: when two moves are within a couple of
 * points, which one the review ranks first is a coin toss (docs/move-review.md).
 */
export type ShownGrade = 'good' | 'inaccuracy' | 'mistake' | 'blunder';
export const shown = (g: Grade): ShownGrade => (g === 'best' ? 'good' : g);
export const SHOWN_GRADES: ShownGrade[] = ['good', 'inaccuracy', 'mistake', 'blunder'];
export const GRADE_LABEL: Record<ShownGrade, string> = {
  good: 'best or good',
  inaccuracy: 'inaccuracy',
  mistake: 'mistake',
  blunder: 'blunder',
};
/** The marks chess annotators use. */
export const GRADE_MARK: Record<ShownGrade, string> = { good: '✓', inaccuracy: '?!', mistake: '?', blunder: '??' };

/** Reviews by event index (the move list's numbering), for the marks in the list. */
export function byEvent(run: GradeRun | null): Map<number, MoveReview> {
  const out = new Map<number, MoveReview>();
  run?.reviews.forEach((r) => r && out.set(r.state, r));
  return out;
}

/**
 * The review to show at a position: the move just made (event pos − 1), or
 * the one about to be made. Null when neither is a graded decision.
 */
export function reviewAt(run: GradeRun | null, pos: number): { review: MoveReview; made: boolean } | null {
  if (!run) return null;
  let next: MoveReview | null = null;
  for (const r of run.reviews) {
    if (!r) continue;
    if (r.state === pos - 1) return { review: r, made: true };
    if (r.state === pos) next = r;
  }
  return next && { review: next, made: false };
}

/** Decisions per shown grade, among those graded so far. */
export function countGrades(run: GradeRun | null): Record<ShownGrade, number> {
  const out: Record<ShownGrade, number> = { good: 0, inaccuracy: 0, mistake: 0, blunder: 0 };
  run?.reviews.forEach((r) => r && out[shown(r.grade)]++);
  return out;
}

/** The moves that lost the most, inaccuracies and worse, biggest first. */
export function worstMoves(run: GradeRun | null, count = 5): MoveReview[] {
  const bad = (run?.reviews ?? []).filter((r): r is MoveReview => !!r && shown(r.grade) !== 'good');
  return bad.sort((a, b) => b.loss - a.loss || a.state - b.state).slice(0, count);
}

// --- Running it ----------------------------------------------------------------

function spawn(): Worker {
  return new Worker(new URL('./grades-worker.ts', import.meta.url), { type: 'module' });
}

// Grades already computed, per record and seat, while the page lives.
const kept = new Map<string, GradeRun>();
const keyOf = (text: string, seat: number) => `${seat}:${text.length}:${text.slice(0, 64)}:${text.slice(-256)}`;

/**
 * Grades every decision of `seat` in a record in the background as soon as
 * it opens. Another seat stops the workers of the old one; what they had
 * finished is kept.
 */
export function useMoveGrades(recordText: string | null, seat: number): GradeRun | null {
  const [run, setRun] = useState<GradeRun | null>(null);

  useEffect(() => {
    setRun(null);
    if (!recordText) return;
    const key = keyOf(recordText, seat);
    const before = kept.get(key);
    if (before && before.decisions.length > 0 && before.reviews.filter(Boolean).length === before.decisions.length) {
      setRun(before);
      return;
    }
    let stopped = false;
    // Decisions are long jobs (about a second each), so all cores help; the
    // chance curve and the setup review share them for the first minute.
    const n = workerCount();
    const workers = Array.from({ length: n }, spawn);
    const t0 = performance.now();
    let live: GradeRun | null = before ? { ...before, reviews: [...before.reviews], workers: n, error: '' } : null;
    if (live) kept.set(key, live);
    let order: number[] = [];
    let next = 0;
    let frame = 0;
    const publish = () => {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        if (!live) return;
        live.seconds = (before?.seconds ?? 0) + (performance.now() - t0) / 1000;
        // A copy for React; `live` itself stays the one being filled (and kept).
        setRun({ ...live, reviews: [...live.reviews] });
      });
    };
    const stop = () => {
      stopped = true;
      workers.forEach((w) => w.terminate());
    };
    const fail = (message: string) => {
      // Not kept: the next visit tries again.
      live = { ...(live ?? { seat, decisions: [], reviews: [], workers: n, seconds: 0 }), error: message };
      publish();
      stop();
    };
    const give = (w: Worker) => {
      if (next < order.length) w.postMessage({ k: order[next++], seed: REVIEW_SEED, budget: BUDGET });
    };
    const finished = () => !!live && live.reviews.filter(Boolean).length === live.decisions.length;

    workers.forEach((w) => {
      w.onerror = (e) => {
        e.preventDefault();
        fail(e.message || 'the review worker could not be loaded');
      };
      w.onmessage = (msg: MessageEvent) => {
        if (stopped) return;
        const data = msg.data as { error?: string; decisions?: Decision[]; k?: number; review?: MoveReview };
        if (typeof data.error === 'string') return fail(data.error);
        if (data.decisions) {
          if (!live) {
            live = { seat, decisions: data.decisions, reviews: [], workers: n, seconds: 0, error: '' };
            kept.set(key, live);
          }
          if (order.length === 0) order = data.decisions.map((_, k) => k).filter((k) => !live?.reviews[k]);
          if (finished()) {
            publish();
            stop();
            return;
          }
          give(w);
          return;
        }
        if (data.review && data.k !== undefined && live) {
          live.reviews[data.k] = data.review;
          publish();
          if (finished()) stop();
          else give(w);
        }
      };
      w.postMessage({ open: recordText, seat });
    });
    return () => {
      stop();
      cancelAnimationFrame(frame);
    };
  }, [recordText, seat]);

  return run;
}
