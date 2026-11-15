// The in-browser benchmark: whole random games in a Web Worker, the same
// games as `catan_selfplay --games N --seed 1` in the native build. The last
// result is kept here so every page can show "your browser" next to the
// published numbers.

import { useSyncExternalStore } from 'react';

export interface BenchResult {
  games: number;
  actions: number;
  refused: number;
  checksum: string;
  seconds: number;
}

let last: BenchResult | null = null;
const listeners = new Set<() => void>();

export function runBenchmark(games: number): Promise<BenchResult> {
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL('./bench-worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = (msg: MessageEvent<BenchResult>) => {
      worker.terminate();
      last = msg.data;
      listeners.forEach((f) => f());
      resolve(msg.data);
    };
    worker.onerror = (err) => {
      worker.terminate();
      reject(new Error(err.message || 'the worker failed'));
    };
    worker.postMessage({ games });
  });
}

/** The last result in this tab, or null before the first run. */
export function useLastBenchmark(): BenchResult | null {
  return useSyncExternalStore(
    (f) => (listeners.add(f), () => listeners.delete(f)),
    () => last,
  );
}
