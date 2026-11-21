// The example game's review, computed ahead of time by
// scripts/review-example.mjs, in the shapes the live runs have (ChanceRun,
// SetupRun), so the start page draws it with the Replay tab's components.

import type { ChanceRun, Counts, ChancePoint } from './chances';
import type { Review, SetupMove, SetupRun } from './setup';

export interface ExampleReviewFile {
  record: string;
  settings: { passes: number; passSize: number; seed: number; setupSeed: number; budget: { first: number; finalists: number; confirm: number } };
  chances: { points: ChancePoint[]; counts: Counts };
  setup: { moves: SetupMove[]; reviews: Review[] };
}

export interface ExampleReview {
  chances: ChanceRun;
  setup: SetupRun;
}

/** The file as two finished runs. */
export function fromFile(file: ExampleReviewFile): ExampleReview {
  const { points, counts } = file.chances;
  const total = points.length * file.settings.passes * file.settings.passSize;
  return {
    chances: { points, counts, done: total, total, workers: 0, seconds: 0, error: '', precomputed: true },
    setup: { moves: file.setup.moves, reviews: file.setup.reviews, workers: 0, seconds: 0, error: '', precomputed: true },
  };
}

let loaded: Promise<ExampleReview> | null = null;

export function exampleReview(): Promise<ExampleReview> {
  loaded ??= fetch(new URL('example-review.json', document.baseURI))
    .then((r) => {
      if (!r.ok) throw new Error(`example-review.json: HTTP ${r.status}`);
      return r.json() as Promise<ExampleReviewFile>;
    })
    .then(fromFile);
  loaded.catch(() => (loaded = null));
  return loaded;
}
