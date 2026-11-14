// The engine's state (indices) turned into catan-project's GameView
// (positions as hex + direction), the shape its Board component draws.
// Only what Board reads is filled in for real; the rest of GameView is the
// live game's (log, timers) and gets neutral values.

import type { Building, GameView, HexCoords, Player, Position, Road } from '@platform/types/game';
import { RESOURCES, type GameRecord, type GameState, type Topology } from './types';

const position = ({ q, r, direction }: { q: number; r: number; direction: number }): Position => ({
  hex: { q, r, s: -q - r },
  direction,
});

export function toGameView(state: GameState, record: GameRecord, topology: Topology): GameView {
  const color = (seat: number) => record.players[seat].color;
  const roads: Road[] = state.players.flatMap((p, seat) =>
    p.roads.map((e) => ({ ...position(topology.edges[e]), color: color(seat) })),
  );
  const settlements: Building[] = state.players.flatMap((p, seat) => [
    ...p.settlements.map((n) => ({ ...position(topology.nodes[n]), owner: color(seat), type: 'settlement' as const })),
    ...p.cities.map((n) => ({ ...position(topology.nodes[n]), owner: color(seat), type: 'city' as const })),
  ]);
  const robber = topology.hexes[state.robber];
  const robberHex: HexCoords | null = robber ? { q: robber.q, r: robber.r, s: -robber.q - robber.r } : null;
  const holder = (seat: number | null) => (seat === null ? null : color(seat));
  return {
    turn_phase: state.phase as GameView['turn_phase'],
    dice_roll: state.dice_roll,
    dice: state.dice ?? [],
    roll_counts: [],
    setup_waiting_for_road: false,
    robber_hex: robberHex,
    is_game_over: state.game_over,
    // Tiles and ports come straight from the record, which uses the same
    // format as the platform.
    board_tiles: record.board.tiles,
    ports: record.board.ports,
    roads,
    settlements,
    longest_road_holder: holder(state.longest_road),
    largest_army_holder: holder(state.largest_army),
    free_roads_left: 0,
    bank: Object.fromEntries(RESOURCES.map((r, i) => [r, state.bank[i]])) as GameView['bank'],
    dev_deck_count: 0,
    trade_offer: null,
    log: [],
    current_seat: state.current,
    winner_seat: state.winner,
  };
}

/** The players as Board names them (by color). */
export function toPlayers(record: GameRecord, state: GameState): Player[] {
  return record.players.map((p) => ({
    seat: p.seat,
    name: p.name,
    is_bot: p.kind === 'bot',
    bot_level: (p.bot_level ?? null) as Player['bot_level'],
    color: p.color,
    victory_points: state.players[p.seat]?.points ?? 0,
    card_count: state.players[p.seat]?.resources.reduce((a, b) => a + b, 0) ?? 0,
    road_length: state.road_lengths[p.seat] ?? 0,
    pending_discard: 0,
    dev_card_count: 0,
    knights_played: state.players[p.seat]?.knights ?? 0,
  }));
}
