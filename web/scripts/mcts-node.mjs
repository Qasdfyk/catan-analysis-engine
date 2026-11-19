// The searching bot in the WebAssembly engine, in Node, on one thread: what
// the web page's workers run. Prints the same "mcts checksum" as the native
// tool for the same game, decisions, budget and seed,
//
//   node scripts/mcts-node.mjs --decisions 4 --iterations 50 --workers 2 ../tests/data/zoo-game-00088.json
//   build/release/tools/catan_mcts --decisions 4 --iterations 50 --workers 2 tests/data/zoo-game-00088.json
//
// so equal checksums mean the browser searches exactly as the native build:
// same drawn hands, same tree, same playouts. Workers run one after another
// here and are merged at the root as the page and the native tool merge them.

import { readFileSync } from 'node:fs';
import createEngine from '../src/wasm/catan_engine.js';

const argv = process.argv.slice(2);
let seed = 1;
let decisions = 8;
let only = -1;
let iterations = 400;
let workers = 1;
let c = 1;
let prior = 0.5;
let elitesPath = new URL('../../data/elites.json', import.meta.url);
const files = [];
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--seed') seed = Number(argv[++i]);
  else if (argv[i] === '--decisions') decisions = Number(argv[++i]);
  else if (argv[i] === '--decision') only = Number(argv[++i]);
  else if (argv[i] === '--iterations') iterations = Number(argv[++i]);
  else if (argv[i] === '--workers') workers = Number(argv[++i]);
  else if (argv[i] === '--c') c = Number(argv[++i]);
  else if (argv[i] === '--prior') prior = Number(argv[++i]);
  else if (argv[i] === '--elites') elitesPath = argv[++i];
  else files.push(argv[i]);
}
if (files.length !== 1) {
  console.error('usage: mcts-node.mjs [--decisions K] [--decision I] [--iterations N] [--workers W] [--seed S] [--c C] [--prior P] [--elites PATH] RECORD.json');
  process.exit(2);
}

const engine = await createEngine();
const refused = engine.setElites(readFileSync(elitesPath, 'utf8'));
if (refused) {
  console.error(refused);
  process.exit(1);
}
const replay = new engine.Replay(readFileSync(files[0], 'utf8'));
if (replay.error()) {
  console.error(replay.error());
  process.exit(1);
}
const list = JSON.parse(replay.mctsDecisionsJson(decisions));
const t0 = performance.now();
// The fold of tools/mcts.cpp, in BigInt: it passes 2^53.
let checksum = 0n;
let total = 0;
list.forEach((_, k) => {
  if (only >= 0 && k !== only) return;
  // Merge the workers at the root: moves in the order first seen.
  const merged = [];
  for (let w = 0; w < workers; w++) {
    const part = JSON.parse(replay.mctsSearch(k, decisions, iterations, seed, w, c, prior));
    total += part.iterations;
    for (const m of part.moves) {
      const same = merged.find((x) => x.move === m.move);
      if (same) {
        same.visits += m.visits;
        same.wins += m.wins;
      } else merged.push({ ...m });
    }
  }
  merged.forEach((m, i) => {
    checksum += (BigInt(m.visits) * 1000n + BigInt(m.wins)) * BigInt(i + 1) * BigInt(k + 1);
  });
});
const seconds = (performance.now() - t0) / 1000;
replay.delete();
console.log(`${list.length} decisions, ${iterations} iterations x ${workers} workers, seed ${seed}, 1 thread (WebAssembly, Node ${process.version})`);
// The native sum is a uint64_t, which wraps.
console.log(`mcts checksum ${BigInt.asUintN(64, checksum)}`);
console.log(`time ${seconds.toFixed(1)} s: ${Math.round(total / seconds)} iterations/s`);
