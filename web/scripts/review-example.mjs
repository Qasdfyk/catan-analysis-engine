// Reviews the example game ahead of time, so the start page shows the chance
// curve, the luck split, the setup review and the grade of every move of the
// person in the game at once, also on a phone. The
// numbers are the ones the Replay tab computes live for the same game: the
// same WebAssembly module, the same seeds, the same playouts per point
// (src/chances.ts, src/setup.ts, src/grades.ts). src/review.test.ts checks a
// sample of them.
//
//   node scripts/review-example.mjs [--threads 15]   # writes public/example-review.json
//
// Build the module first: cmake --preset wasm && cmake --build --preset wasm

import { readFileSync, writeFileSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { gunzipSync } from 'node:zlib';
import createEngine from '../src/wasm/catan_engine.js';

// Keep in step with src/chances.ts and src/setup.ts (the test compares them).
const PASSES = 10;
const PASS_SIZE = 200;
const SEED = 1;
const SETUP_SEED = 1;
const BUDGET = { first: 100, finalists: 4, confirm: 1600 };
const REVIEW_SEED = 1;
const REVIEW_BUDGET = { first: 32, finalists: 2, confirm: 800 };

// Whose moves get graded: the player who came closest without winning
// (src/nav.ts, runnerUp). In the example that is a bot that lost by a
// point; the person in it was out of the game early.
const runnerUp = (record) => {
  const points = record.result?.victory_points ?? [];
  let best = -1;
  record.players.forEach((p) => {
    if (p.seat === record.result?.winner_seat) return;
    if (best < 0 || (points[p.seat] ?? 0) > (points[best] ?? 0)) best = p.seat;
  });
  return Math.max(0, best);
};

const here = (path) => new URL(path, import.meta.url);
const EXAMPLE = 'example.json.gz';
const OUT = here('../public/example-review.json');

async function openEngine(text) {
  const engine = await createEngine();
  const refused = engine.setElites(readFileSync(here('../../data/elites.json'), 'utf8'));
  if (refused) throw new Error(`elites: ${refused}`);
  const replay = new engine.Replay(text);
  if (replay.error()) throw new Error(`the example stops early: ${replay.error()}`);
  return replay;
}

if (!isMainThread) {
  // A worker: chance jobs and setup moves, given by the main thread one at a time.
  const replay = await openEngine(workerData.text);
  const points = JSON.parse(replay.chancePointsJson());
  parentPort.on('message', (job) => {
    if (job.kind === 'move') {
      const review = JSON.parse(
        replay.reviewMove(job.seat, job.k, REVIEW_SEED, REVIEW_BUDGET.first, REVIEW_BUDGET.finalists, REVIEW_BUDGET.confirm),
      );
      parentPort.postMessage({ ...job, review });
    } else if (job.kind === 'chance') {
      const counts = JSON.parse(replay.winChances(points[job.point].state, job.pass * PASS_SIZE, PASS_SIZE, SEED));
      parentPort.postMessage({ ...job, counts });
    } else {
      let review;
      do review = JSON.parse(replay.setupReviewStep(job.move, SETUP_SEED, BUDGET.first, BUDGET.finalists, BUDGET.confirm));
      while (!review.done);
      parentPort.postMessage({ ...job, review });
    }
  });
} else {
  const args = process.argv.slice(2);
  const threads = Number(args[args.indexOf('--threads') + 1] || Math.max(1, availableParallelism() - 1));
  const text = gunzipSync(readFileSync(here(`../public/${EXAMPLE}`))).toString('utf8');
  const replay = await openEngine(text);
  const points = JSON.parse(replay.chancePointsJson());
  const moves = JSON.parse(replay.setupMovesJson());
  const seat = runnerUp(JSON.parse(text));
  const decisions = JSON.parse(replay.reviewDecisionsJson(seat));
  if (decisions.failed) throw new Error(decisions.failed);

  // Setup moves first: they are the longest jobs, then the decisions.
  const jobs = [
    ...moves.map((_, move) => ({ kind: 'setup', move })),
    ...decisions.map((_, k) => ({ kind: 'move', seat, k })),
    ...Array.from({ length: PASSES }, (_, pass) => points.map((_, point) => ({ kind: 'chance', point, pass }))).flat(),
  ];
  const counts = Array.from({ length: PASSES }, () => []);
  const reviews = [];
  const grades = [];
  const t0 = performance.now();
  let next = 0;
  let done = 0;
  await new Promise((resolve, reject) => {
    for (let k = 0; k < threads; k++) {
      const w = new Worker(new URL(import.meta.url), { workerData: { text } });
      const give = () => (next < jobs.length ? w.postMessage(jobs[next++]) : w.terminate());
      w.on('online', give);
      w.on('error', reject);
      w.on('message', (r) => {
        if (r.kind === 'chance') counts[r.pass][r.point] = r.counts;
        else if (r.kind === 'move') grades[r.k] = r.review;
        else reviews[r.move] = r.review;
        if (++done % 200 === 0 || r.kind === 'setup') {
          process.stderr.write(`\r${done} of ${jobs.length} jobs, ${((performance.now() - t0) / 1000).toFixed(0)} s   `);
        }
        if (done === jobs.length) resolve();
        give();
      });
    }
  });
  const seconds = (performance.now() - t0) / 1000;
  const playouts =
    points.length * PASSES * PASS_SIZE + reviews.reduce((a, r) => a + r.playouts, 0) + grades.reduce((a, r) => a + r.playouts, 0);
  process.stderr.write('\n');

  writeFileSync(
    OUT,
    JSON.stringify({
      record: EXAMPLE,
      settings: {
        passes: PASSES,
        passSize: PASS_SIZE,
        seed: SEED,
        setupSeed: SETUP_SEED,
        budget: BUDGET,
        reviewSeed: REVIEW_SEED,
        reviewBudget: REVIEW_BUDGET,
      },
      chances: { points, counts },
      setup: { moves, reviews },
      grades: { seat, decisions, reviews: grades },
    }),
  );
  console.log(`${points.length} chance points, ${moves.length} setup moves, ${decisions.length} decisions, ${playouts} playouts in ${seconds.toFixed(1)} s on ${threads} threads`);
  console.log(`wrote ${OUT.pathname}`);
}
