// Whole random games, and the rules functions the profile of those games
// points at (docs/performance.md). Every benchmark reads positions from one
// shared pool, taken from random games at random moments, so they all
// measure the same kind of position.
//
//   BM_PlayOut      a whole game between random players, from a new board
//   BM_CopyGame     copying the whole state, what every simulation step will do
//   BM_RoadLength   the longest road of each seat (the Longest Road rule)
//   BM_LegalRoads   every place the current seat may build a road

#include "catan/random_player.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <vector>

namespace {

using cae::Game;
using cae::Rng;

// Positions from the middle and end of random games, where roads are long
// and the board is crowded. 256 of them, 768 bytes each: 192 KiB, which fits
// in L2 cache, so the benchmarks time the rules and not main memory.
const std::vector<Game>& positions() {
  static const std::vector<Game> pool = [] {
    std::vector<Game> out;
    Rng rng(2026);
    while (out.size() < 256) {
      Game g = cae::random_game(rng, 4);
      const int stop = 200 + rng.below(400);  // a typical game lasts ~640 steps
      for (int i = 0; i < stop && !g.game_over; ++i) (void)cae::play_random_step(g, rng);
      if (!g.game_over) out.push_back(g);
    }
    return out;
  }();
  return pool;
}

void BM_PlayOut(benchmark::State& state) {
  std::uint64_t seed = 0;
  long long actions = 0;
  for (auto _ : state) {
    Rng rng(++seed);
    Game g = cae::random_game(rng, 4);
    actions += cae::play_out(g, rng).actions;
    benchmark::DoNotOptimize(g.winner);
  }
  state.counters["actions/game"] =
      benchmark::Counter(static_cast<double>(actions), benchmark::Counter::kAvgIterations);
  state.counters["actions/s"] = benchmark::Counter(static_cast<double>(actions), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_PlayOut)->Unit(benchmark::kMicrosecond);

void BM_CopyGame(benchmark::State& state) {
  const auto& pool = positions();
  std::size_t i = 0;
  for (auto _ : state) {
    Game copy = pool[i++ & 255];
    benchmark::DoNotOptimize(copy);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(sizeof(Game)));
}
BENCHMARK(BM_CopyGame);

void BM_RoadLength(benchmark::State& state) {
  const auto& pool = positions();
  std::size_t i = 0;
  for (auto _ : state) {
    const Game& g = pool[i++ & 255];
    for (int s = 0; s < g.player_count; ++s) benchmark::DoNotOptimize(g.road_length(s));
  }
}
BENCHMARK(BM_RoadLength);

void BM_LegalRoads(benchmark::State& state) {
  // The legal roads of the current seat as if it were in the main phase with
  // the cards for a road, so every position has something to compute.
  std::vector<Game> pool = positions();
  for (Game& g : pool) {
    g.phase = cae::Phase::kMain;
    g.players[static_cast<std::size_t>(g.current)].resources = {1, 1, 0, 0, 0};
  }
  std::size_t i = 0;
  for (auto _ : state) {
    const Game& g = pool[i++ & 255];
    benchmark::DoNotOptimize(g.legal_roads(g.current));
  }
}
BENCHMARK(BM_LegalRoads);

}  // namespace
