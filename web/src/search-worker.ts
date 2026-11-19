// One part of a search of the searching bot (include/catan/mcts.hpp), off the
// main thread. search.ts starts several of these on the same decision with
// different worker numbers and adds their root statistics up: root
// parallelization, as `catan_mcts --workers W` does natively.
//
// Messages: {load: true} compiles the module and answers {ready: true};
// {record, decisions, decision, iterations, seed, worker, c, prior} opens the
// game (once per record text) and answers the part's root as mctsSearch()
// gives it. A failure is answered as {error}.

import type { Replay } from '@engine';
// The opponents of the playouts are the MAP-Elites, as in catan_mcts.
import elitesText from '../../data/elites.json?raw';
import { loadEngine } from './engine';

let replay: Replay | null = null;
let opened = '';
let elitesSet = false;

interface Ask {
  load?: true;
  record: string;
  decisions: number;
  decision: number;
  iterations: number;
  seed: number;
  worker: number;
  c: number;
  prior: number;
}

self.onmessage = async (msg: MessageEvent<Ask>) => {
  try {
    const engine = await loadEngine();
    if (!elitesSet) {
      const refused = engine.setElites(elitesText);
      if (refused) throw new Error(`elites: ${refused}`);
      elitesSet = true;
    }
    const d = msg.data;
    if (d.load) {
      self.postMessage({ ready: true });
      return;
    }
    if (!replay || opened !== d.record) {
      replay?.delete();
      replay = new engine.Replay(d.record);
      opened = d.record;
    }
    const text = replay.mctsSearch(d.decision, d.decisions, d.iterations, d.seed, d.worker, d.c, d.prior);
    const part = JSON.parse(text);
    if (!part) throw new Error(`no decision ${d.decision}`);
    self.postMessage({ ...part, decision: JSON.parse(replay.mctsDecisionsJson(d.decisions))[d.decision] });
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? e.message : String(e) });
  }
};
