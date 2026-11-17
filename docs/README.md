# Docs

Design notes and benchmark write-ups land here, one per step, with the numbers and an honest note on what they show and what they do not.

- [rules-engine.md](rules-engine.md): the board as indices and bitmasks, the state, chance as an argument, records.
- [difftest.md](difftest.md): the difference test against catan-project, and how good it is.
- [benchmarks.md](benchmarks.md): getting benchmark numbers you can trust.
- [performance.md](performance.md): games per second against the Python engines, the `perf` profile, two fixes and the data layout.
- [web.md](web.md): the WebAssembly build, the boundary between JavaScript and C++, and the web viewer.
- [zoo.md](zoo.md): the platform's hard bot and its zoo ported from Go, the check that it plays the same moves, its speed, and a first Monte Carlo of the chances to win.
