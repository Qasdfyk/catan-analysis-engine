// The in-browser benchmark: whole games in Web Workers, the same games as
// `catan_selfplay --games N --seed 1 [--bots zoo]` in the native build. The
// last results are kept here so every page can show "your browser" next to
// the published numbers.

import { useSyncExternalStore } from 'react';

/** Random players, or zoo bots (the opponents of the review's simulations). */
export type Bots = 'random' | 'zoo';

export interface BenchResult {
  bots: Bots;
  games: number;
  actions: number;
  refused: number;
  checksum: string;
  seconds: number;
}

/** One point of the scaling run: `workers` workers together. */
export interface ScalingPoint {
  workers: number;
  gamesPerSecond: number;
}

let last: BenchResult | null = null;
let scaling: ScalingPoint[] = [];
const listeners = new Set<() => void>();
const changed = () => listeners.forEach((f) => f());
const subscribe = (f: () => void) => (listeners.add(f), () => listeners.delete(f));

function spawn(): Worker {
  return new Worker(new URL('./bench-worker.ts', import.meta.url), { type: 'module' });
}

/** The worker's script or the engine module could not be fetched. */
class StartError extends Error {}

/**
 * Sends one message and waits for the answer. The worker answers a failure
 * of its own as {error}; an error event without a message means its script
 * never ran, usually a failed download.
 */
function ask<T>(worker: Worker, message: object): Promise<T> {
  return new Promise((resolve, reject) => {
    worker.onmessage = (msg: MessageEvent<T | { error: string }>) => {
      const data = msg.data as { error?: string };
      if (typeof data?.error === 'string') reject(new Error(data.error));
      else resolve(msg.data as T);
    };
    worker.onerror = (err) => {
      err.preventDefault();
      reject(err.message ? new Error(err.message) : new StartError('the benchmark worker could not be loaded'));
    };
    worker.postMessage(message);
  });
}

/**
 * A worker with the engine compiled and ready. Starting is tried twice: just
 * after a deploy the CDN in front of GitHub Pages can fail one request for a
 * new file, and a second request a moment later succeeds.
 */
export async function startWorker(make: () => Worker = spawn, attempts = 2): Promise<Worker> {
  for (let attempt = 1; ; attempt++) {
    const worker = make();
    try {
      await ask(worker, { load: true });
      return worker;
    } catch (e) {
      worker.terminate();
      if (!(e instanceof StartError) || attempt >= attempts) throw e;
    }
  }
}

export async function runBenchmark(games: number, bots: Bots = 'random'): Promise<BenchResult> {
  const worker = await startWorker();
  try {
    last = await ask<BenchResult>(worker, { games, seed: 1, bots });
    changed();
    return last;
  } finally {
    worker.terminate();
  }
}

/** 1, 2, 4, ... up to the number of logical cores the browser reports (at most 32). */
export function workerCounts(cores = navigator.hardwareConcurrency || 4): number[] {
  const counts: number[] = [];
  for (let n = 1; n <= Math.min(cores, 32); n *= 2) counts.push(n);
  if (counts.at(-1)! < Math.min(cores, 32)) counts.push(Math.min(cores, 32));
  return counts;
}

/**
 * Plays `gamesPerWorker` games on 1, 2, 4, ... workers at once and reports
 * games per second for each count. Each worker gets its own seed, so they
 * play different games, like threads of the native build. The clock starts
 * once every worker has compiled the module and stops when the last one is
 * done, so it measures the games, not the start-up.
 */
export async function runScaling(gamesPerWorker: number, onPoint?: (p: ScalingPoint) => void): Promise<ScalingPoint[]> {
  scaling = [];
  changed();
  for (const n of workerCounts()) {
    const started = await Promise.allSettled(Array.from({ length: n }, () => startWorker()));
    const workers = started.flatMap((s) => (s.status === 'fulfilled' ? [s.value] : []));
    try {
      const failed = started.find((s) => s.status === 'rejected');
      if (failed) throw failed.reason;
      const start = performance.now();
      await Promise.all(workers.map((w, i) => ask(w, { games: gamesPerWorker, seed: i + 1 })));
      const seconds = (performance.now() - start) / 1000;
      const point = { workers: n, gamesPerSecond: (n * gamesPerWorker) / seconds };
      scaling = [...scaling, point];
      changed();
      onPoint?.(point);
    } finally {
      workers.forEach((w) => w.terminate());
    }
  }
  return scaling;
}

/** The last single-worker result in this tab, or null before the first run. */
export function useLastBenchmark(): BenchResult | null {
  return useSyncExternalStore(subscribe, () => last);
}

/** The scaling points measured so far in this tab. */
export function useScaling(): ScalingPoint[] {
  return useSyncExternalStore(subscribe, () => scaling);
}
