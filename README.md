# catan-analysis-engine

Post-game review for Catan-style board games, in the spirit of a chess game review: step through a finished game, see a grade for every decision, the better move when there was one, and how each player's chances of winning changed, with the swings from dice kept apart from the swings from decisions.

Work in progress. So far there is the rules engine: the full rules of catan-project in C++ (board as bitmasks, production, robber, development cards, trading between players), reading and replaying its game records, a difference test that holds it to the Python rules after every move, and a random player that plays about 10 000 whole games a second on one core, some 4 000 times faster per move than the Python rules. See [docs/rules-engine.md](docs/rules-engine.md), [docs/difftest.md](docs/difftest.md) and [docs/performance.md](docs/performance.md).

## Plan

1. Game records exported from [catan-project](https://github.com/Qasdfyk/catan-project), the browser game this engine is tested against.
2. A rules engine in C++20, checked move by move against the Python rules of catan-project, fast enough to play many thousands of games a second.
3. A WebAssembly build, so the review runs in the browser and the site can be static.
4. Win chances from simulated games, then a search bot, then a small neural network trained by self-play.
5. Move grades from the drop in win chances against the best move, judged only on what the player could see at the time.

The engine only looks at finished games. It gives no advice during a game.

## Build

Needs CMake 3.25+, a C++20 compiler (GCC or Clang) and network access on the first configure, which downloads nlohmann/json, GoogleTest, Google Benchmark and (for the `python` preset) pybind11 at pinned versions.

```sh
cmake --preset release          # configure into build/release
cmake --build --preset release -j"$(nproc)"
ctest --preset release
```

Presets:

| Preset | What |
|---|---|
| `debug` | Debug build with AddressSanitizer and UBSan |
| `release` | Release build with the default compiler |
| `clang-release` | Release build with `clang++` |
| `profile` | Release build with debug info and frame pointers, for `perf` |
| `python` | The `cae_py` module for the difference test, built for catan-project's backend Python |

`scripts/check.sh` runs the first three (configure, build, test), runs the benchmarks briefly to prove they work, then builds the `python` preset and runs the difference test against catan-project, which must be checked out next to this repo. GitHub Actions is off for this repo, so this script is the check before merging. `scripts/check.sh --bench` also runs the benchmarks for real.

Replay a game record downloaded from the platform:

```sh
build/release/tools/catan_replay catan-AB12CD.json
```

Play random games and see how fast they go:

```sh
build/release/tools/catan_selfplay --games 20000 --threads 8
```

Run one benchmark:

```sh
build/release/benchmarks/catan_benchmarks --benchmark_filter=BM_TouchingPopcount
```

See [docs/benchmarks.md](docs/benchmarks.md) for getting numbers you can trust, and [docs/performance.md](docs/performance.md) for the results and the profile.

## Layout

- `include/catan/`: public headers of the `catan` library (namespace `cae`).
- `src/`: library sources.
- `tests/`: unit tests (GoogleTest).
- `benchmarks/`: microbenchmarks (Google Benchmark).
- `tools/`: `catan_replay` (replay a record), `catan_selfplay` (random games, games per second), `difftest.py` (the difference test) and `bench_python.py` (games per second of the Python engines).
- `python/`: the pybind11 module the difference test loads.
- `cmake/`: dependency pins.
- `scripts/check.sh`: the local CI.
- `docs/`: notes.

## License

MIT. Catan is a trademark of its owner; this project is not affiliated with it.
