// Runs selfplay off the main thread, so the page stays responsive while the
// engine plays thousands of games. The worker loads its own copy of the
// module: workers share no memory with the page.
//
// Messages: {load: true} only compiles the module and answers {ready: true},
// so a scaling run can start every worker's clock together; {games, seed}
// plays and answers with selfplay's result; {bots: 'zoo'} plays zoo bots
// instead of random players (zooSelfplay). A failure is answered as
// {error: message}: an exception in an async handler would otherwise never
// reach the page, and the benchmark would wait forever.

import { loadEngine } from './engine';

self.onmessage = async (msg: MessageEvent<{ load?: true; games?: number; seed?: number; bots?: 'random' | 'zoo' }>) => {
  try {
    const engine = await loadEngine();
    if (msg.data.load) {
      self.postMessage({ ready: true });
      return;
    }
    const play = msg.data.bots === 'zoo' ? engine.zooSelfplay : engine.selfplay;
    self.postMessage({ ...JSON.parse(play(msg.data.games ?? 1000, 4, msg.data.seed ?? 1)), bots: msg.data.bots ?? 'random' });
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? e.message : String(e) });
  }
};
