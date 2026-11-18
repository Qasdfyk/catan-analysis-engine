// Types for the module Emscripten builds from wasm/bindings.cpp. Embind
// objects live in the WebAssembly heap: call delete() when done with one,
// the JavaScript garbage collector does not free C++ memory.
declare module '@engine' {
  export interface Replay {
    eventCount(): number;
    stateCount(): number;
    error(): string;
    stateJson(i: number): string;
    boardJson(): string;
    chancePointsJson(): string;
    winChances(state: number, first: number, count: number, seed: number): string;
    delete(): void;
  }
  export interface EngineModule {
    Replay: new (recordText: string) => Replay;
    topologyJson(): string;
    selfplay(games: number, players: number, seed: number): string;
    zooSelfplay(games: number, players: number, seed: number): string;
  }
  const createEngine: (overrides?: object) => Promise<EngineModule>;
  export default createEngine;
}
