// Win chances along one game in the WebAssembly engine, in Node, on one
// thread: the playouts the web page runs in its workers. Prints the same
// "wins checksum" as the native tool for the same playouts and seed,
//
//   node scripts/winrate-node.mjs --playouts 100 ../tests/data/zoo-game-00088.json
//   build/release/tools/catan_winrate --playouts 100 tests/data/zoo-game-00088.json
//
// so equal checksums mean the browser estimates exactly what the native
// build does.

import { readFileSync } from 'node:fs';
import createEngine from '../src/wasm/catan_engine.js';

const argv = process.argv.slice(2);
let playouts = 100;
let seed = 1;
const files = [];
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--playouts') playouts = Number(argv[++i]);
  else if (argv[i] === '--seed') seed = Number(argv[++i]);
  else files.push(argv[i]);
}
if (files.length !== 1) {
  console.error('usage: winrate-node.mjs [--playouts N] [--seed S] RECORD.json');
  process.exit(2);
}

const engine = await createEngine();
const replay = new engine.Replay(readFileSync(files[0], 'utf8'));
if (replay.error()) {
  console.error(replay.error());
  process.exit(1);
}
const points = JSON.parse(replay.chancePointsJson());
const t0 = performance.now();
// BigInt: the sum can pass 2^53.
let checksum = 0n;
points.forEach((p, i) => {
  const w = JSON.parse(replay.winChances(p.state, 0, playouts, seed));
  w.wins.forEach((n, s) => (checksum += BigInt(n) * BigInt(i + 1) * BigInt(s + 1)));
});
const seconds = (performance.now() - t0) / 1000;
replay.delete();
console.log(`${points.length} points, ${playouts} playouts each, seed ${seed}, 1 thread (WebAssembly, Node ${process.version})`);
console.log(`wins checksum ${checksum}`);
console.log(`time ${seconds.toFixed(2)} s: ${Math.round((points.length * playouts) / seconds)} playouts/s`);
