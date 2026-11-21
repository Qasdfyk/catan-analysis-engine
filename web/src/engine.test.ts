// The real WebAssembly module, run in Node: every test record and every
// example game shipped with the page must replay to its recorded result.

import { readFileSync, readdirSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import { readRecord } from './adapters';
import { EXAMPLES, OpenGame, loadEngine, topology } from './engine';
import { NATIVE_ZOO_CHECKSUMS } from './facts';

const dataDir = new URL('../../tests/data/', import.meta.url);
const records = readdirSync(dataDir)
  .filter((f) => f.endsWith('.json'))
  .map((f) => ({ name: f, text: readFileSync(new URL(f, dataDir), 'utf8') }));
for (const { file } of EXAMPLES) {
  records.push({ name: file, text: gunzipSync(readFileSync(new URL(`../public/${file}`, import.meta.url))).toString('utf8') });
}

describe('engine in WebAssembly', () => {
  it.each(records)('replays $name to the recorded result', async ({ text }) => {
    const game = new OpenGame(await loadEngine(), text);
    try {
      expect(game.error).toBe('');
      expect(game.stateCount).toBe(game.record.events.length + 1);
      const last = game.state(game.stateCount - 1);
      const result = game.record.result!;
      expect(last.game_over).toBe(true);
      expect(last.winner).toBe(result.winner_seat);
      expect(last.turn).toBe(result.turns);
      expect(last.players.map((p) => p.points + p.hidden_points)).toEqual(result.victory_points);
    } finally {
      game.close();
    }
  });

  it('reports a broken record instead of throwing', async () => {
    const engine = await loadEngine();
    const replay = new engine.Replay('{"format": "something else"}');
    expect(replay.error()).not.toBe('');
    expect(replay.stateCount()).toBe(0);
    expect(replay.stateJson(0)).toBe('null');
    replay.delete();
  });

  it('gives the whole topology', async () => {
    const t = await topology();
    expect(t.hexes).toHaveLength(19);
    expect(t.nodes).toHaveLength(54);
    expect(t.edges).toHaveLength(72);
  });

  it('plays the same random games for the same seed', async () => {
    const engine = await loadEngine();
    const a = JSON.parse(engine.selfplay(50, 4, 7));
    const b = JSON.parse(engine.selfplay(50, 4, 7));
    expect(a.refused).toBe(0);
    expect(a.checksum).toBe(b.checksum);
  });

  it('plays the zoo games of the native build', async () => {
    const engine = await loadEngine();
    const r = JSON.parse(engine.zooSelfplay(500, 4, 1));
    expect(r.refused).toBe(0);
    expect(r.checksum).toBe(NATIVE_ZOO_CHECKSUMS[500]);
  });

  it('ships zoo examples won by the archetype they are named after', () => {
    for (const { file } of EXAMPLES.filter((x) => x.file.startsWith('zoo/'))) {
      const record = JSON.parse(records.find((r) => r.name === file)!.text);
      const winner = record.players[record.result.winner_seat];
      expect(winner.bot_style.archetype).toBe(file.slice(4, -'.json.gz'.length));
    }
  });

  it('ships an expert example won by the expert', () => {
    const record = JSON.parse(records.find((r) => r.name === 'expert/expert-wins.json.gz')!.text);
    expect(record.players[record.result.winner_seat].bot_level).toBe('expert');
  });

  it('reads every test record and example through the adapters, unchanged', async () => {
    for (const { name, text } of records) {
      const read = await readRecord(text);
      expect(read.text, name).toBe(text);
      expect(read.from).toBe('a catan-project record');
    }
  });
});
