// The engine as the page sees it (types of the module: wasm-module.d.ts): one loaded module, records opened into
// ReplayLog objects (C++), states parsed from JSON on demand.

import createEngine, { type EngineModule, type Replay } from '@engine';
import { readRecord } from './adapters';
import type { BoardInfo, GameRecord, GameState, Topology } from './types';

let module: Promise<EngineModule> | null = null;

/** Loads and compiles the WebAssembly module once. */
export function loadEngine(): Promise<EngineModule> {
  module ??= createEngine();
  return module;
}

/** A record opened in the engine: every state of the game, one per event. */
export class OpenGame {
  readonly record: GameRecord;
  readonly board: BoardInfo;
  readonly stateCount: number;
  /** "" when the engine accepted every event, else why it stopped. */
  readonly error: string;
  private readonly replay: Replay;
  // Parsing a state is cheap but not free; the slider asks for the same
  // states again and again while it is dragged.
  private readonly cache = new Map<number, GameState>();

  constructor(engine: EngineModule, text: string) {
    this.record = JSON.parse(text) as GameRecord;
    this.replay = new engine.Replay(text);
    this.error = this.replay.error();
    this.stateCount = this.replay.stateCount();
    this.board = JSON.parse(this.replay.boardJson()) as BoardInfo;
  }

  state(i: number): GameState {
    let s = this.cache.get(i);
    if (!s) {
      s = JSON.parse(this.replay.stateJson(i)) as GameState;
      this.cache.set(i, s);
    }
    return s;
  }

  /** Frees the C++ side. The object is unusable afterwards. */
  close(): void {
    this.replay.delete();
    this.cache.clear();
  }
}

let topo: Promise<Topology> | null = null;

/** Board positions behind the engine's indices; the same for every game. */
export function topology(): Promise<Topology> {
  topo ??= loadEngine().then((engine) => JSON.parse(engine.topologyJson()) as Topology);
  return topo;
}

/** Games shipped with the page, in public/. The first opens by default. */
export const EXAMPLES = [
  { file: 'example.json.gz', label: 'Example game', note: 'a human against three hard bots' },
  { file: 'zoo/ore-wheat.json.gz', label: 'Zoo: ore-wheat wins', note: 'cities and development cards' },
  { file: 'zoo/wood-brick.json.gz', label: 'Zoo: wood-brick wins', note: 'settlements and the Longest Road' },
  { file: 'zoo/port.json.gz', label: 'Zoo: port wins', note: 'one resource and the port that sells it' },
  { file: 'zoo/balanced.json.gz', label: 'Zoo: balanced wins', note: 'the hard bot, shaken a little' },
  { file: 'expert/expert-wins.json.gz', label: 'Expert wins', note: 'the searching bot against three hard bots on the platform' },
] as const;

const examples = new Map<string, Promise<string>>();

/** The text of a record shipped with the page; by default the example game. */
export function exampleRecord(file: string = EXAMPLES[0].file): Promise<string> {
  let text = examples.get(file);
  if (!text) {
    text = fetch(new URL(file, document.baseURI))
      .then((response) => {
        if (!response.ok) throw new Error(`${file}: HTTP ${response.status}`);
        return response.arrayBuffer();
      })
      .then(async (bytes) => (await readRecord(bytes)).text);
    text.catch(() => examples.delete(file));
    examples.set(file, text);
  }
  return text;
}
