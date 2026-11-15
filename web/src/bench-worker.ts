// Runs selfplay off the main thread, so the page stays responsive while the
// engine plays thousands of games. The worker loads its own copy of the
// module: workers share no memory with the page.
//
// Messages: {load: true} only compiles the module and answers {ready: true},
// so a scaling run can start every worker's clock together; {games, seed}
// plays and answers with selfplay's result.

import { loadEngine } from './engine';

self.onmessage = async (msg: MessageEvent<{ load?: true; games?: number; seed?: number }>) => {
  const engine = await loadEngine();
  if (msg.data.load) {
    self.postMessage({ ready: true });
    return;
  }
  self.postMessage(JSON.parse(engine.selfplay(msg.data.games ?? 1000, 4, msg.data.seed ?? 1)));
};
