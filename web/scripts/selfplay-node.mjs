// The WebAssembly engine in Node: the same games as the native
// `catan_selfplay --games N --seed S [--bots zoo]`, printed in the same
// format, so the two can be compared line by line (checksum) and by speed.
//
//   node scripts/selfplay-node.mjs --games 20000
//   node scripts/selfplay-node.mjs --games 2000 --bots zoo
//
// Build the module first: cmake --preset wasm && cmake --build --preset wasm

import createEngine from '../src/wasm/catan_engine.js';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((pairs, arg, i, all) => (i % 2 === 0 ? [...pairs, [arg.replace(/^--/, ''), all[i + 1]]] : pairs), []),
);
const games = Number(args.games ?? 10000);
const players = Number(args.players ?? 4);
const seed = Number(args.seed ?? 1);
const bots = args.bots ?? 'random';
if (bots !== 'random' && bots !== 'zoo') {
  console.error('--bots is random or zoo');
  process.exit(2);
}

const engine = await createEngine();
const r = JSON.parse(bots === 'zoo' ? engine.zooSelfplay(games, players, seed) : engine.selfplay(games, players, seed));
console.log(`${r.games} games, ${bots} bots, ${players} players, seed ${seed}, 1 thread (WebAssembly, Node ${process.version})`);
console.log(`  ${(r.actions / r.games).toFixed(1)} actions per game, ${r.refused} refused actions`);
console.log(`  checksum ${r.checksum}`);
console.log(
  `time ${r.seconds.toFixed(3)} s: ${Math.round(r.games / r.seconds)} games/s, ` +
    `${(r.actions / r.seconds / 1e6).toFixed(2)} M actions/s, ${((r.seconds / r.games) * 1e6).toFixed(1)} us per game`,
);
process.exit(r.refused === 0 ? 0 : 1);
