// The move review of one seat in the WebAssembly engine, in Node, on one
// thread: what the web page's workers run. Prints the same "review checksum"
// as the native tool for the same game, seat, seed, budget and elites,
//
//   node scripts/review-node.mjs --seat 0 ../tests/data/zoo-game-00088.json
//   build/release/tools/catan_review --seat 0 tests/data/zoo-game-00088.json
//
// so equal checksums mean the browser grades every decision exactly as the
// native build does: same moves, same values, same grade, same "why".

import { readFileSync } from 'node:fs';
import createEngine from '../src/wasm/catan_engine.js';

const argv = process.argv.slice(2);
let seat = 0;
let seed = 1;
let first = 32;
let finalists = 2;
let confirm = 800;
let elitesPath = new URL('../../data/elites.json', import.meta.url);
const files = [];
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--seat') seat = Number(argv[++i]);
  else if (argv[i] === '--seed') seed = Number(argv[++i]);
  else if (argv[i] === '--first') first = Number(argv[++i]);
  else if (argv[i] === '--finalists') finalists = Number(argv[++i]);
  else if (argv[i] === '--confirm') confirm = Number(argv[++i]);
  else if (argv[i] === '--elites') elitesPath = argv[++i];
  else files.push(argv[i]);
}
if (files.length !== 1) {
  console.error('usage: review-node.mjs [--seat N] [--seed S] [--first N] [--finalists K] [--confirm N] [--elites PATH] RECORD.json');
  process.exit(2);
}

const engine = await createEngine();
const refused = engine.setElites(readFileSync(elitesPath, 'utf8'));
if (refused) {
  console.error(refused);
  process.exit(1);
}
const replay = new engine.Replay(readFileSync(files[0], 'utf8'));
const decisions = JSON.parse(replay.reviewDecisionsJson(seat));
if (decisions.failed) {
  console.error(decisions.failed);
  process.exit(1);
}
const t0 = performance.now();
// FNV-1a over every review's JSON text, as tools/review.cpp folds it. The
// text is the module's own (nlohmann::json::dump), so both sides hash the
// same bytes; the reviews are ASCII.
let checksum = 0xcbf29ce484222325n;
let playouts = 0;
const grades = {};
for (let k = 0; k < decisions.length; k++) {
  const text = replay.reviewMove(seat, k, seed, first, finalists, confirm);
  const r = JSON.parse(text);
  if (!r || r.failed) {
    console.error(`decision ${k}: ${r ? r.failed : 'null'}`);
    process.exit(1);
  }
  playouts += r.playouts;
  grades[r.grade] = (grades[r.grade] ?? 0) + 1;
  for (let i = 0; i < text.length; i++) {
    checksum = BigInt.asUintN(64, (checksum ^ BigInt(text.charCodeAt(i))) * 0x100000001b3n);
  }
}
const seconds = (performance.now() - t0) / 1000;
replay.delete();
console.log(`${decisions.length} decisions of seat ${seat}, seed ${seed}, budget ${first}/${finalists}/${confirm}, 1 thread (WebAssembly, Node ${process.version})`);
console.log(`review checksum ${checksum.toString(16).padStart(16, '0')}`);
console.log(`grades ${JSON.stringify(grades)}`);
console.log(`time ${seconds.toFixed(1)} s: ${(seconds / Math.max(1, decisions.length)).toFixed(2)} s a decision, ${Math.round(playouts / seconds)} playouts/s`);
