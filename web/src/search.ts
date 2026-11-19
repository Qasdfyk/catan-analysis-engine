// The searching bot (include/catan/mcts.hpp) in the browser: one decision of
// the example game searched on 1, 2, 4, ... Web Workers at once, each running
// its own part (search-worker.ts), the parts added up at the root. More
// workers mean more playouts in the same time, so the measure is playouts per
// second; the merged root shows what the bot thinks of the decision.

import { useSyncExternalStore } from 'react';
import { ask, startWorker, workerCounts } from './bench';
import { exampleRecord } from './engine';

/** catan_mcts --decisions 8 --decision 3 --iterations 250: a main-phase decision of the example game with 20 moves. */
export const SEARCH = { decisions: 8, decision: 3, iterations: 250, seed: 1, c: 1, prior: 0.5 } as const;

export interface RootMove {
  move: string;
  kind: string;
  target: number;
  prior: number;
  visits: number;
  wins: number;
}

export interface Part {
  moves: RootMove[];
  iterations: number;
  seconds: number;
  decision: { state: number; seat: number; phase: string };
}

/** Adds parts up at the root, moves in the order first seen: merge_searches() of mcts.cpp. */
export function mergeParts(parts: Pick<Part, 'moves'>[]): RootMove[] {
  const merged: RootMove[] = [];
  for (const p of parts) {
    for (const m of p.moves) {
      const same = merged.find((x) => x.move === m.move);
      if (same) {
        same.visits += m.visits;
        same.wins += m.wins;
      } else merged.push({ ...m });
    }
  }
  return merged;
}

/** The "mcts checksum" of catan_mcts for one decision (index `decision` of the spread). */
export function searchChecksum(moves: RootMove[], decision: number): string {
  let sum = 0n;
  moves.forEach((m, i) => {
    sum += (BigInt(m.visits) * 1000n + BigInt(m.wins)) * BigInt(i + 1) * BigInt(decision + 1);
  });
  return BigInt.asUintN(64, sum).toString();
}

export interface SearchPoint {
  workers: number;
  playoutsPerSecond: number;
  moves: RootMove[];
  checksum: string;
  decision: Part['decision'];
}

let points: SearchPoint[] = [];
const listeners = new Set<() => void>();
const changed = () => listeners.forEach((f) => f());
const subscribe = (f: () => void) => (listeners.add(f), () => listeners.delete(f));

function spawn(): Worker {
  return new Worker(new URL('./search-worker.ts', import.meta.url), { type: 'module' });
}

/**
 * Searches the decision on 1, 2, 4, ... workers. Each count is a separate
 * search of SEARCH.iterations playouts per worker; the clock starts once
 * every worker is ready and stops at the last answer.
 */
export async function runSearchScaling(): Promise<SearchPoint[]> {
  points = [];
  changed();
  const record = await exampleRecord();
  for (const n of workerCounts()) {
    const started = await Promise.allSettled(Array.from({ length: n }, () => startWorker(spawn)));
    const workers = started.flatMap((s) => (s.status === 'fulfilled' ? [s.value] : []));
    try {
      const failed = started.find((s) => s.status === 'rejected');
      if (failed) throw failed.reason;
      const start = performance.now();
      const parts = await Promise.all(workers.map((w, i) => ask<Part>(w, { ...SEARCH, record, worker: i })));
      const seconds = (performance.now() - start) / 1000;
      const moves = mergeParts(parts);
      points = [
        ...points,
        {
          workers: n,
          playoutsPerSecond: (n * SEARCH.iterations) / seconds,
          moves,
          checksum: searchChecksum(moves, SEARCH.decision),
          decision: parts[0].decision,
        },
      ];
      changed();
    } finally {
      workers.forEach((w) => w.terminate());
    }
  }
  return points;
}

/** The points measured so far in this tab. */
export function useSearchPoints(): SearchPoint[] {
  return useSyncExternalStore(subscribe, () => points);
}
