// The setup review of one game in the WebAssembly engine, in Node, on one
// thread: what the web page's workers run. Prints the same "setup checksum"
// as the native tool for the same game, seed and elites,
//
//   node scripts/setup-node.mjs ../tests/data/zoo-game-00088.json
//   build/release/tools/catan_setup tests/data/zoo-game-00088.json
//
// so equal checksums mean the browser reviews the setup exactly as the
// native build does: same corners, same rounds, same playouts.

import { readFileSync } from 'node:fs';
import createEngine from '../src/wasm/catan_engine.js';

const argv = process.argv.slice(2);
let seed = 1;
let first = 100;
let finalists = 4;
let confirm = 1600;
let elitesPath = new URL('../../data/elites.json', import.meta.url);
const files = [];
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--seed') seed = Number(argv[++i]);
  else if (argv[i] === '--first') first = Number(argv[++i]);
  else if (argv[i] === '--finalists') finalists = Number(argv[++i]);
  else if (argv[i] === '--confirm') confirm = Number(argv[++i]);
  else if (argv[i] === '--elites') elitesPath = argv[++i];
  else files.push(argv[i]);
}
if (files.length !== 1) {
  console.error('usage: setup-node.mjs [--seed S] [--first N] [--finalists K] [--confirm N] [--elites PATH] RECORD.json');
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
const moves = JSON.parse(replay.setupMovesJson());
const t0 = performance.now();
// The fold of tools/setup.cpp, in BigInt: it passes 2^53.
let checksum = 0n;
let playouts = 0;
moves.forEach((_, m) => {
  let r;
  do r = JSON.parse(replay.setupReviewStep(m, seed, first, finalists, confirm));
  while (!r.done);
  playouts += r.playouts;
  r.candidates.forEach((c, k) => {
    c.halvingWins.forEach((h, s) => {
      checksum += (BigInt(h) * 7n + BigInt(c.confirmWins[s])) * BigInt(k + 1) * BigInt(s + 1) * BigInt(m + 1);
    });
  });
});
const seconds = (performance.now() - t0) / 1000;
replay.delete();
console.log(`${moves.length} setup moves, seed ${seed}, budget ${first}/${finalists}/${confirm}, 1 thread (WebAssembly, Node ${process.version})`);
// The native sum is a uint64_t, which wraps.
console.log(`setup checksum ${BigInt.asUintN(64, checksum)}`);
console.log(`time ${seconds.toFixed(1)} s: ${Math.round(playouts / seconds)} playouts/s`);
