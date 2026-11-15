// The engine as the page sees it (types of the module: wasm-module.d.ts): one loaded module, records opened into
// ReplayLog objects (C++), states parsed from JSON on demand.

import createEngine, { type EngineModule, type Replay } from '@engine';
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

let example: Promise<string> | null = null;

/** The text of the example record shipped with the page (public/example.json.gz). */
export function exampleRecord(): Promise<string> {
  example ??= fetch(new URL('example.json.gz', document.baseURI))
    .then((response) => {
      if (!response.ok) throw new Error(`example game: HTTP ${response.status}`);
      return response.arrayBuffer();
    })
    .then(readRecordBytes);
  example.catch(() => (example = null));
  return example;
}

/** Plain JSON or gzip (what the platform downloads), told apart by the first two bytes. */
export async function readRecordBytes(bytes: ArrayBuffer): Promise<string> {
  const head = new Uint8Array(bytes, 0, Math.min(2, bytes.byteLength));
  if (head[0] === 0x1f && head[1] === 0x8b) {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
    return await new Response(stream).text();
  }
  return new TextDecoder().decode(bytes);
}
