// Review of the initial placement, estimated in Web Workers (setup-worker.ts)
// by the engine's successive halving (include/catan/setup_review.hpp): for
// every settlement of the setup, the chance to win from every corner the
// player could have taken.
//
// Each worker takes whole moves and answers after every round, so the heat
// map of a move first shows every corner roughly, then sharpens on the best
// ones. A game of four has 8 setup moves, so up to 8 workers help.

import { useEffect, useRef, useState } from 'react';
import { workerCount } from './chances';

/** catan_setup's defaults: 100 playouts per corner in round 0, 4 finalists, 1600 to confirm. */
export const BUDGET = { first: 100, finalists: 4, confirm: 1600 } as const;
export const SETUP_SEED = 1;

export interface SetupMove {
  /** State index just before the settlement. */
  state: number;
  seat: number;
  node: number;
  /** 0: the seat's first settlement, 1: its second. */
  round: number;
}

export interface Candidate {
  node: number;
  value: number;
  playouts: number;
  dropped: boolean;
  finalist: boolean;
}

export interface Review {
  done: boolean;
  rounds: number;
  playouts: number;
  seat: number;
  /** Index into candidates, -1 if none. */
  played: number;
  best: number;
  candidates: Candidate[];
  /** After done: the loss of the corner played against the best one, and its standard error. */
  loss?: { loss: number; error: number };
}

/**
 * The setup move to show at a state: the settlement about to be placed, or
 * the one just placed while its road is still to come. Null outside the setup.
 */
export function focusMove(moves: SetupMove[], pos: number): number | null {
  for (let i = moves.length - 1; i >= 0; i--) {
    if (pos === moves[i].state || pos === moves[i].state + 1) return i;
  }
  return null;
}

/** The best value among the corners still in (the halving's own estimate until done). */
export function bestValue(review: Review): number {
  if (review.done && review.best >= 0) return review.candidates[review.best].value;
  let top = 0;
  for (const c of review.candidates) if (!c.dropped) top = Math.max(top, c.value);
  return top;
}

export type Verdict = 'best' | 'noise' | 'loss';

/**
 * Best: the corner played is the best one. Noise: it loses, but by less
 * than the 95% interval of the difference. Loss: it loses by more.
 */
export function verdict(review: Review): Verdict | null {
  if (!review.done || !review.loss || review.played < 0) return null;
  if (review.played === review.best) return 'best';
  return review.loss.loss <= 1.96 * review.loss.error ? 'noise' : 'loss';
}

/** Colour of a corner by how much it is behind the best: green at 0, red from 15 pp. */
export function heatColor(behind: number): string {
  const t = Math.min(1, Math.max(0, behind / 0.15));
  return `hsl(${Math.round(120 * (1 - t))} 70% 50%)`;
}

// --- Running it ----------------------------------------------------------------

export interface SetupRun {
  moves: SetupMove[];
  /** reviews[move], as far as it got. */
  reviews: (Review | undefined)[];
  workers: number;
  seconds: number;
  error: string;
}

function spawn(): Worker {
  return new Worker(new URL('./setup-worker.ts', import.meta.url), { type: 'module' });
}

/** Reviews the setup of a record in the background as soon as it opens. */
export function useSetupReview(recordText: string | null): SetupRun | null {
  const [run, setRun] = useState<SetupRun | null>(null);
  const live = useRef<SetupRun | null>(null);

  useEffect(() => {
    setRun(null);
    if (!recordText) return;
    let stopped = false;
    // One move per worker at a time, so more than 8 would idle. The chance
    // curve runs alongside for its first minute; the cores are shared.
    const n = Math.max(1, Math.min(8, workerCount()));
    const workers = Array.from({ length: n }, spawn);
    const t0 = performance.now();
    let next = 0;
    let frame = 0;
    const publish = () => {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        if (live.current) setRun({ ...live.current, reviews: [...live.current.reviews], seconds: (performance.now() - t0) / 1000 });
      });
    };
    const stop = () => {
      stopped = true;
      workers.forEach((w) => w.terminate());
    };
    const fail = (message: string) => {
      live.current ??= { moves: [], reviews: [], workers: n, seconds: 0, error: '' };
      live.current.error = message;
      publish();
      stop();
    };
    const give = (w: Worker) => {
      if (!live.current || next >= live.current.moves.length) return;
      w.postMessage({ move: next++, seed: SETUP_SEED, budget: BUDGET });
    };

    workers.forEach((w) => {
      w.onerror = (e) => {
        e.preventDefault();
        fail(e.message || 'the setup worker could not be loaded');
      };
      w.onmessage = (msg: MessageEvent) => {
        if (stopped) return;
        const data = msg.data as { error?: string; moves?: SetupMove[]; move?: number; review?: Review };
        if (typeof data.error === 'string') return fail(data.error);
        if (data.moves) {
          live.current ??= { moves: data.moves, reviews: [], workers: n, seconds: 0, error: '' };
          if (data.moves.length === 0) {
            publish();
            stop();
            return;
          }
          give(w);
          return;
        }
        if (data.review && data.move !== undefined && live.current) {
          live.current.reviews[data.move] = data.review;
          publish();
          if (data.review.done) {
            give(w);
            if (live.current.reviews.filter((r) => r?.done).length === live.current.moves.length) stop();
          }
        }
      };
      w.postMessage({ open: recordText });
    });
    return () => {
      stop();
      cancelAnimationFrame(frame);
      live.current = null;
    };
  }, [recordText]);

  return run;
}
