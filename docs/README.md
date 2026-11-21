# Docs

Design notes and benchmark write-ups land here, one per step, with the numbers and an honest note on what they show and what they do not.

- [plan.md](plan.md): every step with its status, and why the Colonist step is conditional.

- [rules-engine.md](rules-engine.md): the board as indices and bitmasks, the state, chance as an argument, records.
- [difftest.md](difftest.md): the difference test against catan-project, and how good it is.
- [benchmarks.md](benchmarks.md): getting benchmark numbers you can trust.
- [performance.md](performance.md): games per second against the Python engines, the `perf` profile, two fixes and the data layout.
- [web.md](web.md): the WebAssembly build, the boundary between JavaScript and C++, the web viewer, and "Review this game".
- [converters.md](converters.md): the adapter interface that turns records from other places into the engine's format.
- [zoo.md](zoo.md): the platform's hard bot and its zoo ported from Go, the check that it plays the same moves, its speed, and a first Monte Carlo of the chances to win.
- [win-chances.md](win-chances.md): chances to win along a game by Monte Carlo, the split into luck and decisions and its noise, calibration with bootstrap intervals, speed native and in the browser.
- [arena.md](arena.md): the arena with SPRT, Catanatron through a bridge, validation set A with a baseline model, sanity positions C.
- [map-elites.md](map-elites.md): MAP-Elites over the zoo, the best bot of every style; the winner's curse and re-evaluation; the Robber and the leader.
- [setup-review.md](setup-review.md): the initial placement graded corner by corner: successive halving, a confirmation round against the winner's curse, and whether a better setup wins more often.
- [mcts.md](mcts.md): the searching bot: ISMCTS over its own turn, what a seat knows of the hidden cards and states drawn to match it, PUCT with the zoo bot as a prior, tuning, the arena, speed and root parallelization.
- [expert.md](expert.md): the searching bot as the platform's expert level, a separate process fed the game's record, and golden positions B: 1 200 decisions labelled once by deep Monte Carlo.
