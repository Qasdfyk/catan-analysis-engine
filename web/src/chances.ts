// Chances to win along a game, estimated in Web Workers (chances-worker.ts)
// by the engine's Monte Carlo (include/catan/winrate.hpp), and what the page
// computes from them: the curve, and how much of each player's swing came
// from the dice (luck) and how much from the moves in between (decisions).
//
// The playouts of every point are done in PASSES of equal size: pass k plays
// playouts k*size .. (k+1)*size - 1 at every point. After each pass the curve
// covers the whole game and gets sharper with the next. Passes use disjoint
// playouts, so each is an independent estimate of the whole game; the spread
// of the luck/decision split between passes says how far to trust it (batch
// means).

import { useEffect, useRef, useState } from 'react';

/** PointKind of winrate.hpp. */
export const Kind = { Start: 0, BeforeRoll: 1, AfterRoll: 2, End: 3 } as const;

export interface ChancePoint {
  /** State index of the replay. */
  state: number;
  kind: (typeof Kind)[keyof typeof Kind];
  turn: number;
  seat: number;
}

export interface WinCounts {
  wins: number[];
  unfinished: number;
  playouts: number;
}

export const PASSES = 10;
export const PASS_SIZE = 200;
export const SEED = 1;

export interface Job {
  point: number;
  pass: number;
}

/** Every point of pass 0, then every point of pass 1, and so on. */
export function planJobs(points: number, passes = PASSES): Job[] {
  const jobs: Job[] = [];
  for (let pass = 0; pass < passes; pass++) for (let point = 0; point < points; point++) jobs.push({ point, pass });
  return jobs;
}

/** counts[pass][point], filled as results come in. */
export type Counts = (WinCounts | undefined)[][];

/** Chance of each seat at each point from all playouts so far; null where none came in yet. */
export function chancesFrom(counts: Counts, points: number, seats: number): (number[] | null)[] {
  const out: (number[] | null)[] = [];
  for (let i = 0; i < points; i++) {
    const wins = new Array<number>(seats).fill(0);
    let n = 0;
    for (const pass of counts) {
      const c = pass[i];
      if (!c) continue;
      c.wins.forEach((w, s) => (wins[s] += w));
      n += c.playouts;
    }
    out.push(n > 0 ? wins.map((w) => w / n) : null);
  }
  return out;
}

export interface Split {
  start: number[];
  luck: number[];
  decisions: number[];
  end: number[];
}

/**
 * Luck: the change across each roll (point before roll -> after roll).
 * Decisions: every other change, setup included. Per seat, start + luck +
 * decisions = end exactly. Needs every point.
 */
export function split(points: ChancePoint[], chances: number[][]): Split {
  const seats = chances[0].length;
  const luck = new Array<number>(seats).fill(0);
  const decisions = new Array<number>(seats).fill(0);
  for (let i = 1; i < points.length; i++) {
    const to = points[i].kind === Kind.AfterRoll ? luck : decisions;
    for (let s = 0; s < seats; s++) to[s] += chances[i][s] - chances[i - 1][s];
  }
  return { start: chances[0], luck, decisions, end: chances[chances.length - 1] };
}

/** The passes that are complete (every point counted). */
export function completePasses(counts: Counts, points: number): number[] {
  // A loop, not every(): every() skips the holes of a sparse array, and the
  // results of a pass arrive in any order.
  const complete = (pass: Counts[number]) => {
    for (let i = 0; i < points; i++) if (!pass[i]) return false;
    return true;
  };
  return counts.flatMap((pass, k) => (complete(pass) ? [k] : []));
}

/**
 * Standard error of the luck of each seat: the luck of every complete pass
 * on its own, then sd / sqrt(passes). Decisions have the same error, since
 * luck + decisions is fixed by the start and the end. Null under 3 passes.
 */
export function luckError(points: ChancePoint[], counts: Counts, seats: number): number[] | null {
  const done = completePasses(counts, points.length);
  if (done.length < 3) return null;
  const lucks = done.map((k) => split(points, chancesFrom([counts[k]], points.length, seats) as number[][]).luck);
  return Array.from({ length: seats }, (_, s) => {
    const xs = lucks.map((l) => l[s]);
    const mean = xs.reduce((a, b) => a + b, 0) / xs.length;
    const variance = xs.reduce((a, x) => a + (x - mean) ** 2, 0) / (xs.length - 1);
    return Math.sqrt(variance / xs.length);
  });
}

/** What happened to chances in the turn around `state`: the roll and the decisions after it. */
export function turnAt(points: ChancePoint[], chances: (number[] | null)[], state: number) {
  // The last "after roll" point at or before the state, and the points around it.
  let i = -1;
  for (let k = 0; k < points.length; k++) {
    if (points[k].state > state) break;
    if (points[k].kind === Kind.AfterRoll) i = k;
  }
  if (i < 1) return null;
  const before = chances[i - 1];
  const after = chances[i];
  const next = chances[i + 1];
  if (!before || !after) return null;
  return {
    seat: points[i].seat,
    turn: points[i].turn,
    roll: after.map((c, s) => c - before[s]),
    decisions: next ? next.map((c, s) => c - after[s]) : null,
  };
}

// --- Running it ----------------------------------------------------------------

export interface ChanceRun {
  points: ChancePoint[];
  counts: Counts;
  /** Playouts done, and all there will be. */
  done: number;
  total: number;
  workers: number;
  seconds: number;
  error: string;
  /** Computed ahead of time (the start page's example), not in this browser. */
  precomputed?: boolean;
}

function spawn(): Worker {
  return new Worker(new URL('./chances-worker.ts', import.meta.url), { type: 'module' });
}

/** How many workers: one core is left to the page. */
export const workerCount = () => Math.max(1, Math.min(8, (navigator.hardwareConcurrency || 4) - 1));

/**
 * Estimates the chances of a record in the background as soon as it opens.
 * A new record (or leaving the page) stops the workers of the old one.
 */
export function useWinChances(recordText: string | null): ChanceRun | null {
  const [run, setRun] = useState<ChanceRun | null>(null);
  const live = useRef<ChanceRun | null>(null);

  useEffect(() => {
    setRun(null);
    if (!recordText) return;
    let stopped = false;
    const n = workerCount();
    const workers = Array.from({ length: n }, spawn);
    const t0 = performance.now();
    let jobs: Job[] = [];
    let next = 0;
    let frame = 0;
    // Results arrive hundreds of times a second; React redraws at most once a frame.
    const publish = () => {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        // Also after the last result stopped the workers: that one counts too.
        if (live.current) setRun({ ...live.current, seconds: (performance.now() - t0) / 1000 });
      });
    };
    const fail = (message: string) => {
      if (!live.current) live.current = { points: [], counts: [], done: 0, total: 0, workers: n, seconds: 0, error: '' };
      live.current.error = message;
      publish();
      stop();
    };
    const stop = () => {
      stopped = true;
      workers.forEach((w) => w.terminate());
    };
    const give = (w: Worker) => {
      const job = jobs[next++];
      if (!job || !live.current) return;
      w.postMessage({ state: live.current.points[job.point].state, point: job.point, pass: job.pass, first: job.pass * PASS_SIZE, count: PASS_SIZE, seed: SEED });
    };

    workers.forEach((w, k) => {
      w.onerror = (e) => {
        e.preventDefault();
        fail(e.message || 'the win-chance worker could not be loaded');
      };
      w.onmessage = (msg: MessageEvent) => {
        if (stopped) return;
        const data = msg.data as { error?: string; points?: ChancePoint[]; point?: number; pass?: number; counts?: WinCounts };
        if (typeof data.error === 'string') return fail(data.error);
        if (data.points) {
          // The first worker to open the record says where the points are.
          if (!live.current) {
            jobs = planJobs(data.points.length);
            live.current = {
              points: data.points,
              counts: Array.from({ length: PASSES }, () => []),
              done: 0,
              total: data.points.length * PASSES * PASS_SIZE,
              workers: n,
              seconds: 0,
              error: '',
            };
          }
          give(w);
          return;
        }
        if (data.counts && live.current && data.point !== undefined && data.pass !== undefined) {
          live.current.counts[data.pass][data.point] = data.counts;
          live.current.done += data.counts.playouts;
          publish();
          give(w);
          if (live.current.done >= live.current.total) {
            stop();
          }
        }
      };
      w.postMessage({ open: recordText, worker: k });
    });
    return () => {
      stop();
      cancelAnimationFrame(frame);
      live.current = null;
    };
  }, [recordText]);

  return run;
}
