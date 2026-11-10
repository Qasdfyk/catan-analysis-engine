# catan-analysis-engine

Post-game review for Catan-style board games, in the spirit of a chess game review: step through a finished game, see a grade for every decision, the better move when there was one, and how each player's chances of winning changed, with the swings from dice kept apart from the swings from decisions.

Work in progress. So far there is the build skeleton: CMake presets, tests, benchmarks and a first piece of the board representation (sets of corners as bitmasks). There is no rules engine yet.

## Plan

1. Game records exported from [catan-project](https://github.com/Qasdfyk/catan-project), the browser game this engine is tested against.
2. A rules engine in C++20, checked move by move against the Python rules of catan-project, fast enough to play many thousands of games a second.
3. A WebAssembly build, so the review runs in the browser and the site can be static.
4. Win chances from simulated games, then a search bot, then a small neural network trained by self-play.
5. Move grades from the drop in win chances against the best move, judged only on what the player could see at the time.

The engine only looks at finished games. It gives no advice during a game.

## Build

Needs CMake 3.25+, a C++20 compiler (GCC or Clang) and network access on the first configure, which downloads GoogleTest and Google Benchmark at pinned versions.

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

`scripts/check.sh` runs all three (configure, build, test), then runs the benchmarks briefly to prove they work. GitHub Actions is off for this repo, so this script is the check before merging. `scripts/check.sh --bench` also runs the benchmarks for real.

Run one benchmark:

```sh
build/release/benchmarks/catan_benchmarks --benchmark_filter=BM_TouchingPopcount
```

See [docs/benchmarks.md](docs/benchmarks.md) for getting numbers you can trust.

## Layout

- `include/catan/`: public headers of the `catan` library (namespace `cae`).
- `src/`: library sources.
- `tests/`: unit tests (GoogleTest).
- `benchmarks/`: microbenchmarks (Google Benchmark).
- `cmake/`: dependency pins.
- `scripts/check.sh`: the local CI.
- `docs/`: notes.

## License

MIT. Catan is a trademark of its owner; this project is not affiliated with it.
