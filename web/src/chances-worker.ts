// Plays win-chance playouts off the main thread (chances.ts runs several of
// these). Each worker opens the record in its own copy of the engine:
// workers share no memory with the page or with each other.
//
// Messages: {open: recordText} opens the game and answers {points}; then
// {state, point, pass, first, count, seed} plays those playouts from that
// state and answers {point, pass, counts}. A failure is answered as {error}.

import type { Replay } from '@engine';
import { loadEngine } from './engine';

let replay: Replay | null = null;

self.onmessage = async (msg: MessageEvent) => {
  try {
    const engine = await loadEngine();
    const d = msg.data as { open?: string; state: number; point: number; pass: number; first: number; count: number; seed: number };
    if (d.open !== undefined) {
      replay?.delete();
      // A record the rules stopped early still has its states up to there.
      replay = new engine.Replay(d.open);
      self.postMessage({ points: JSON.parse(replay.chancePointsJson()) });
      return;
    }
    if (!replay) throw new Error('no game open');
    const counts = JSON.parse(replay.winChances(d.state, d.first, d.count, d.seed));
    self.postMessage({ point: d.point, pass: d.pass, counts });
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? e.message : String(e) });
  }
};
