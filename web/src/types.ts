// Shapes of the JSON that crosses from the engine (wasm/bindings.cpp) and of
// the game record itself (docs/game-record.md in catan-project).

export const RESOURCES = ['wood', 'brick', 'sheep', 'wheat', 'ore'] as const;
export type Resource = (typeof RESOURCES)[number];

export const DEV_CARDS = ['knight', 'victory_point', 'road_building', 'year_of_plenty', 'monopoly'] as const;
export type DevCard = (typeof DEV_CARDS)[number];

/** Card counts in RESOURCES order. */
export type CardArray = [number, number, number, number, number];
/** Card counts by name, as in record payloads ({"wood": 1}). */
export type CardMap = Partial<Record<Resource, number>>;

export interface RecordPlayer {
  seat: number;
  name: string;
  color: 'red' | 'blue' | 'white' | 'orange';
  kind: 'human' | 'bot';
  bot_level: string | null;
  /** A zoo bot from catan-project's tournament: its style, weights and trading. */
  bot_style?: { archetype: string; personality: string; seed: number };
}

export interface RecordEvent {
  turn: number;
  seat: number;
  action: string;
  payload: Record<string, unknown>;
  outcome?: Record<string, unknown>;
  visible_to?: number[];
}

export interface RecordTile {
  hex: { q: number; r: number; s: number };
  resource: Resource | 'desert';
  number: number | null;
}

export interface GameRecord {
  format: string;
  version: number;
  source: string;
  game_id: string | null;
  players: RecordPlayer[];
  board: { tiles: RecordTile[]; ports: { type: string; vertices: { hex: { q: number; r: number; s: number }; direction: number }[] }[] };
  events: RecordEvent[];
  result: { winner_seat: number | null; victory_points: number[]; turns: number } | null;
}

/** topologyJson(): the position behind each index, in the spelling of game records. */
export interface Topology {
  hexes: { q: number; r: number }[];
  nodes: { q: number; r: number; direction: number; hexes: number[] }[];
  edges: { q: number; r: number; direction: number; nodes: [number, number] }[];
}

/** boardJson(): the part of the board that never changes. */
export interface BoardInfo {
  hexes: { terrain: Resource | 'desert'; number: number }[];
  ports: { type: string; nodes: [number, number] }[];
}

export interface PlayerState {
  resources: CardArray;
  points: number;
  hidden_points: number;
  dev_cards: CardArray;
  new_dev_cards: CardArray;
  knights: number;
  settlements: number[];
  cities: number[];
  roads: number[];
}

/** stateJson(i): snapshot() from record.cpp plus bank and dice. */
export interface GameState {
  players: PlayerState[];
  current: number;
  turn: number;
  phase: string;
  dice_roll: number | null;
  dice: [number, number] | null;
  robber: number;
  offer: { proposer: number; give: CardArray; get: CardArray; accepted: number[]; declined: number[] } | null;
  largest_army: number | null;
  longest_road: number | null;
  road_lengths: number[];
  bank: CardArray;
  game_over: boolean;
  winner: number | null;
}
