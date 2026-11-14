// Runs selfplay off the main thread, so the page stays responsive while the
// engine plays thousands of games. The worker loads its own copy of the
// module: workers share no memory with the page.

import { loadEngine } from './engine';

self.onmessage = async (msg: MessageEvent<{ games: number }>) => {
  const engine = await loadEngine();
  const result = JSON.parse(engine.selfplay(msg.data.games, 4, 1));
  self.postMessage(result);
};
