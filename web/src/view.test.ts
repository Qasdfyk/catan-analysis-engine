// The adapter from engine indices to the platform's positions: every piece
// built in a record must land where the record says, in the record's own
// spelling (records use the canonical spelling, as topologyJson() does).

import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import { OpenGame, loadEngine, topology } from './engine';
import { toGameView, toPlayers } from './view';

const text = gunzipSync(readFileSync(new URL('../public/example.json.gz', import.meta.url))).toString('utf8');
const key = (p: { hex: { q: number; r: number }; direction: number }) => `${p.hex.q},${p.hex.r},${p.direction}`;

describe('toGameView', () => {
  it('puts every built piece where the record built it', async () => {
    const game = new OpenGame(await loadEngine(), text);
    const topo = await topology();
    const end = toGameView(game.state(game.stateCount - 1), game.record, topo);
    const roads = new Set(end.roads.map(key));
    const buildings = new Map(end.settlements.map((b) => [key(b), b]));
    for (const e of game.record.events) {
      const at = e.payload as { hex: { q: number; r: number }; direction: number };
      const color = game.record.players[e.seat].color;
      if (e.action === 'build_road') expect(roads.has(key(at))).toBe(true);
      if (e.action === 'build_settlement') expect(buildings.get(key(at))?.owner).toBe(color);
      if (e.action === 'upgrade_city') expect(buildings.get(key(at))?.type).toBe('city');
    }
    expect(end.roads.length).toBe(game.record.events.filter((e) => e.action === 'build_road').length);
    game.close();
  });

  it('places the robber and names the players by color', async () => {
    const game = new OpenGame(await loadEngine(), text);
    const topo = await topology();
    const start = game.state(0);
    const view = toGameView(start, game.record, topo);
    const desert = game.record.board.tiles.find((t) => t.resource === 'desert')!;
    expect(view.robber_hex).toEqual(desert.hex);
    expect(toPlayers(game.record, start).map((p) => p.color)).toEqual(game.record.players.map((p) => p.color));
    game.close();
  });
});
