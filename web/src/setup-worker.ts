// Reviews setup settlements off the main thread (setup.ts runs several of
// these). Each worker opens the record in its own copy of the engine and
// reviews whole moves, one round at a time (include/catan/setup_review.hpp).
//
// Messages: {open: recordText} opens the game and answers {moves}; then
// {move, seed, budget} reviews that move and answers {move, review} after every
// round, the last one with review.done. A failure is answered as {error}.

import type { Replay } from '@engine';
// The MAP-Elites archive the native tool reads, bundled as text: playouts
// seat the same bots in the browser as in catan_setup.
import elitesText from '../../data/elites.json?raw';
import { loadEngine } from './engine';

let replay: Replay | null = null;
let elitesSet = false;

self.onmessage = async (msg: MessageEvent) => {
  try {
    const engine = await loadEngine();
    if (!elitesSet) {
      const refused = engine.setElites(elitesText);
      if (refused) throw new Error(`elites: ${refused}`);
      elitesSet = true;
    }
    const d = msg.data as { open?: string; move: number; seed: number; budget: { first: number; finalists: number; confirm: number } };
    if (d.open !== undefined) {
      replay?.delete();
      replay = new engine.Replay(d.open);
      self.postMessage({ moves: JSON.parse(replay.setupMovesJson()) });
      return;
    }
    if (!replay) throw new Error('no game open');
    for (;;) {
      const text = replay.setupReviewStep(d.move, d.seed, d.budget.first, d.budget.finalists, d.budget.confirm);
      const review = JSON.parse(text);
      if (!review) throw new Error(`no setup move ${d.move}`);
      self.postMessage({ move: d.move, review });
      if (review.done) break;
    }
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? e.message : String(e) });
  }
};
