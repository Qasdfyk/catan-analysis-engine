// Grades decisions of one player off the main thread (grades.ts runs
// several of these). Each worker opens the record in its own copy of the
// engine and grades whole decisions (include/catan/review.hpp).
//
// Messages: {open: recordText, seat} opens the game and answers
// {decisions}; then {k, seed, budget} grades decision k of that seat and
// answers {k, review}. A failure is answered as {error}.

import type { Replay } from '@engine';
// The MAP-Elites archive the native tool reads, bundled as text: playouts
// seat the same bots in the browser as in catan_review.
import elitesText from '../../data/elites.json?raw';
import { loadEngine } from './engine';

let replay: Replay | null = null;
let seat = 0;
let elitesSet = false;

self.onmessage = async (msg: MessageEvent) => {
  try {
    const engine = await loadEngine();
    if (!elitesSet) {
      const refused = engine.setElites(elitesText);
      if (refused) throw new Error(`elites: ${refused}`);
      elitesSet = true;
    }
    const d = msg.data as { open?: string; seat?: number; k: number; seed: number; budget: { first: number; finalists: number; confirm: number } };
    if (d.open !== undefined) {
      replay?.delete();
      replay = new engine.Replay(d.open);
      seat = d.seat ?? 0;
      const decisions = JSON.parse(replay.reviewDecisionsJson(seat));
      if (decisions.failed) throw new Error(decisions.failed);
      self.postMessage({ decisions });
      return;
    }
    if (!replay) throw new Error('no game open');
    const review = JSON.parse(replay.reviewMove(seat, d.k, d.seed, d.budget.first, d.budget.finalists, d.budget.confirm));
    if (!review) throw new Error(`no decision ${d.k}`);
    if (review.failed) throw new Error(review.failed);
    self.postMessage({ k: d.k, review });
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? e.message : String(e) });
  }
};
