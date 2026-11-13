# Performance

How fast the engine plays whole games, how that compares with the Python engines, where the time goes, and what the data layout has to do with it. Step B8 of the plan.

## The yardstick: random games

`catan_selfplay` plays complete four-player games between random players and reports games per second:

```sh
build/release/tools/catan_selfplay --games 20000
build/release/tools/catan_selfplay --games 200000 --threads 8
```

The player (`src/random_player.cpp`) is a port of catan-project's `random_player.py`, decision for decision: it prefers moves that score, buys cards, trades with the bank and, one turn in ten, with another player. Because the Python engine runs the same policy, "games per second" compares the engines, not the players. A check that the port is faithful: both play about 640 actions and 200 turns per game (C++ 636 and 197, Python 646 and 201).

The random board follows `Board.create_standard_game()`: shuffled terrain and numbers until no 6 and 8 touch, and the nine ports at fixed gaps along the coast.

The random numbers come from our own `Rng` (`include/catan/rng.hpp`, xoshiro256** with Lemire's unbiased range reduction), not from `std::mt19937` with `std::uniform_int_distribution`. The standard fixes the Mersenne Twister's output but not how the distribution maps it to a range, and GCC and Clang do it differently. With our own `below()`, one seed is one game on every compiler and in WebAssembly. Game *i* of a run uses the stream of (seed, *i*), so the checksum printed by `catan_selfplay` does not depend on the number of threads. It is the quick test that an optimisation changed nothing: same seed, same games, same checksum.

## Results

Measured on 2026-11-13. The machine was an i7-11800H (8 cores, 16 threads) under WSL2, with GCC 15, the `release` preset (`-O3`, generic x86-64) and one thread pinned with `taskset -c 2`. Each row is the median of three runs of 20 000 games. Python is 3.12.

| Engine | Games/s | Time per game | Actions/s | Actions per game |
|---|---:|---:|---:|---:|
| catan-project `game.py` | 2.4 | 411 ms | 1 600 | 646 |
| Catanatron 3.2.1 (its `RandomPlayer`) | 27.9 | 35.9 ms | 26 700 | 960 |
| this engine, before B8's two fixes | 4 120 | 243 µs | 2.6 M | 636 |
| this engine, after | **10 800** | **93 µs** | **6.8 M** | 636 |

Per action, the C++ engine is about 4 000 times faster than `game.py` and about 250 times faster than Catanatron. Catanatron's random player picks uniformly among all legal actions, so it builds less often and its games are half again as long. Compare actions per second between it and us, not games.

The Python numbers come from `tools/bench_python.py`. It runs `game.py` with catan-project's backend Python, and Catanatron (GPL-3.0, not part of this repository) from its own virtual environment. The script's docstring shows how.

### Threads

Games are independent, so `--threads` splits them into blocks with nothing shared:

| Threads | 1 | 2 | 4 | 8 | 16 |
|---|---:|---:|---:|---:|---:|
| Games/s | 10 600 | 18 700 | 31 200 | 45 600 | 50 600 |
| Speed-up | 1.0 | 1.8 | 2.9 | 4.3 | 4.8 |

The scaling is not linear even though the threads share no data. With one busy core the CPU turbo-boosts that core, and with eight busy cores each runs at a lower clock. Past 8 threads the extra threads are hyper-threads sharing a core's execution units with another thread, and a compute-bound loop like this one leaves little idle for them to use.

### Compilers

Clang 21 (`clang-release`) gives about 10 000 games/s against GCC's 10 800, within the run-to-run noise of a laptop. Neither compiler is clearly faster for this code.

## Where the time went: the profile

`perf` samples the program about 5 000 times a second and records which function was running. The `profile` preset is a Release build with `-g` (symbols and line numbers) and `-fno-omit-frame-pointer` (so `perf -g` can walk the call stack):

```sh
cmake --preset profile && cmake --build --preset profile --target catan_selfplay
perf stat -e cycles:u,instructions:u,branch-misses:u,L1-dcache-load-misses:u \
  build/profile/tools/catan_selfplay --games 5000
perf record -F 4999 -g build/profile/tools/catan_selfplay --games 5000
perf report --no-children
```

On Ubuntu, `perf` comes from `sudo apt install linux-perf`. Under WSL2 the hardware counters work for user-space code (`:u`), which is all we need.

### Before

`perf stat` showed 4.4 billion cycles and 15.7 billion instructions for 5 000 games. That is **3.55 instructions per cycle**, close to what the core can retire. There were only 150 000 L1 data-cache misses, practically none, and 1% of branches were mispredicted. The program was not waiting for memory. It was doing too much work: about 4 900 instructions per action.

`perf report` said where:

| Function | Share of samples |
|---|---:|
| `longest_trail_from` (the Longest Road search) | 45% |
| `check_road_spot` (called 72 times per `legal_roads`) | 12% |
| `__popcountdi2` (popcount as a library call) | 8% |
| `occupied` | 6% |
| everything else | 29% |

### Fix 1: compute the longest road less often

`game.py` recomputes every player's longest road after every road and every settlement, and the C++ port did the same. A player's longest road depends only on their own roads and on opponents' buildings that cut them. Two things follow:

- A new road changes only its owner's length.
- A new settlement can only cut the road of a player who has a road at that corner. Usually nobody does, and then nothing needs recomputing.

`update_longest_road` now takes the set of seats to recompute (`src/game.cpp`), and its callers pass only those. `road_length` itself also got cheaper:
- it starts the search once per corner, not once per road end (two roads meeting at a corner used to start the same search twice);
- it computes the opponents' buildings once, not at every step of the recursion;
- it stops when a trail has used every road, because none can be longer.

The search stays a plain depth-first search over all trails. With at most 15 roads and 3 sides per corner, the search itself was never the problem; calling it four times too often was.

Result: 4 120 → 7 230 games/s. `road_length` for all four seats went from 5.7 µs to 3.1 µs (`BM_RoadLength`).

### Fix 2: legal roads as masks

`free_road_edges` asked `check_road_spot` about each of the 72 sides. Each call recomputed the occupied corners and looped over every player's roads. The same rule written for all sides at once:

```
from  = own buildings | (ends of own roads & ~occupied corners)
legal = (sides touching `from`) & ~(every built road)
```

Plus the setup rule (the road must touch the settlement just placed) as one more mask. `BM_LegalRoads` went from 801 ns to 27 ns, 30 times faster, and the whole run from 7 230 to 10 800 games/s.

### Did the fixes change any game?

No. The same seed gives the same checksum before and after (5 000 games, seed 1: `98544073`). The difference test against `game.py` (17 recorded games and 200 random games with probes, `scripts/check.sh --games 200`) found no differences.

### After

There are now 1 320 instructions per action, down from 4 900, and 2.6 times the speed overall. The profile is flatter:

| Function | Share |
|---|---:|
| `longest_trail_from` + `road_length` | 26% |
| `play_random_step` (the player itself) | 14% |
| `bank_count` + `trade_rate` | 16% |
| `produce` | 6% |
| `__popcountdi2` | 7.5% |

Next candidates, when they matter:
- `bank_count` and `trade_rate` are mostly the random player asking about every pair of resources before a bank trade. Bots in the zoo (step 9b) will ask differently, so this waits for them.
- `__popcountdi2` is `std::popcount` compiled for the generic x86-64 baseline, which has no `POPCNT` instruction (see `include/catan/bits.hpp`). Whether to target a newer baseline is an open question, and the lesson's exercise measures it.
- The longest road could be maintained incrementally instead of searched. That is worth doing only if the simulations of step 10 show it at the top again.

## Data layout

`Game` is one flat struct of fixed-size arrays and bitmasks, 768 bytes with GCC on x86-64. There are no pointers and no heap. It is 12 cache lines, and copying it is one `memcpy`: `BM_CopyGame` measures 21 ns per copy, about 34 GiB/s, from L2 cache. Simulations will copy a position before each playout, and a 21 ns copy is about 0.02% of a 93 µs game, so the copy is not worth optimising.

The profile backs up the layout. With practically no cache misses and 3.5 instructions per cycle, the state of a game sits in L1 cache for the whole game. Array-of-structs versus struct-of-arrays and similar layout questions do not matter at this size. What mattered was the amount of work: recomputing the same answers. That is the usual lesson of profiling: measure first, then fix the algorithm before the bytes.

Where layout will matter is many games at once: thousands of positions in a search tree, or batches of positions for the neural network. That is a question for steps 12 and 14.

## Benchmarks

`benchmarks/game_bench.cpp` holds the benchmarks behind the tables above. `BM_PlayOut` plays whole games; `BM_CopyGame`, `BM_RoadLength` and `BM_LegalRoads` time single functions over a pool of 256 mid-game positions taken from random games. Rules for trustworthy numbers are in [benchmarks.md](benchmarks.md).

```sh
taskset -c 2 build/release/benchmarks/catan_benchmarks --benchmark_filter='PlayOut|RoadLength|LegalRoads|Copy' \
  --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```
