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

let last: BenchResult | null = null;
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
export function ask<T>(worker: Worker, message: object): Promise<T> {
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

/** The last single-worker result in this tab, or null before the first run. */
export function useLastBenchmark(): BenchResult | null {
  return useSyncExternalStore(subscribe, () => last);
}
