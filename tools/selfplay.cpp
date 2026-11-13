// catan_selfplay: plays whole games between random players and reports how
// fast the engine runs them. This is the "games per second" of
// docs/performance.md.
//
//   build/release/tools/catan_selfplay --games 20000
//   build/release/tools/catan_selfplay --games 200000 --threads 16
//
// Game i always uses the random stream of (seed, i), so a seed gives the same
// games, and the same checksum, whatever the number of threads.

#include "catan/random_player.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Totals {
  long long games = 0;
  long long actions = 0;
  long long turns = 0;
  long long unfinished = 0;
  long long refused = 0;
  std::uint64_t checksum = 0;  // order-independent, so threads do not change it
  std::array<long long, cae::kMaxPlayers> wins{};
};

void play_range(int first, int last, int players, std::uint64_t seed, Totals& out) {
  for (int i = first; i < last; ++i) {
    // Seed and game number go into separate halves; splitmix64 inside Rng
    // turns neighboring seeds into unrelated streams.
    cae::Rng rng((seed << 32) ^ static_cast<std::uint64_t>(i));
    cae::Game game = cae::random_game(rng, players);
    const cae::PlayoutResult r = cae::play_out(game, rng);
    ++out.games;
    out.actions += r.actions;
    out.turns += game.turn_number;
    out.refused += r.refused;
    if (r.winner < 0) {
      ++out.unfinished;
    } else {
      ++out.wins[static_cast<std::size_t>(r.winner)];
    }
    out.checksum += static_cast<std::uint64_t>(r.actions) * 31 + static_cast<std::uint64_t>(r.winner + 1);
  }
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr, "usage: %s [--games N] [--players 2..4] [--seed S] [--threads T]\n", argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  int games = 10'000;
  int players = 4;
  std::uint64_t seed = 1;
  int threads = 1;
  for (int i = 1; i < argc; ++i) {
    if (i + 1 >= argc) usage(argv[0]);
    const std::string flag = argv[i];
    const char* value = argv[++i];
    if (flag == "--games") {
      games = std::atoi(value);
    } else if (flag == "--players") {
      players = std::atoi(value);
    } else if (flag == "--seed") {
      seed = std::strtoull(value, nullptr, 10);
    } else if (flag == "--threads") {
      threads = std::atoi(value);
    } else {
      usage(argv[0]);
    }
  }
  if (games < 1 || players < 2 || players > 4 || threads < 1) usage(argv[0]);
  threads = std::min(threads, games);

  // Each thread gets a contiguous block of games and its own totals, so the
  // threads share nothing while they run (no locks, no atomics).
  std::vector<Totals> per_thread(static_cast<std::size_t>(threads));
  std::vector<std::thread> pool;
  const auto t0 = std::chrono::steady_clock::now();
  for (int t = 0; t < threads; ++t) {
    const int first = static_cast<int>(static_cast<long long>(games) * t / threads);
    const int last = static_cast<int>(static_cast<long long>(games) * (t + 1) / threads);
    pool.emplace_back(play_range, first, last, players, seed, std::ref(per_thread[static_cast<std::size_t>(t)]));
  }
  for (auto& th : pool) th.join();
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  Totals all;
  for (const Totals& t : per_thread) {
    all.games += t.games;
    all.actions += t.actions;
    all.turns += t.turns;
    all.unfinished += t.unfinished;
    all.refused += t.refused;
    all.checksum += t.checksum;
    for (std::size_t s = 0; s < all.wins.size(); ++s) all.wins[s] += t.wins[s];
  }

  const auto per_game = [&](long long n) { return static_cast<double>(n) / static_cast<double>(all.games); };
  std::printf("%lld games, %d players, seed %llu, %d thread%s\n", all.games, players,
              static_cast<unsigned long long>(seed), threads, threads == 1 ? "" : "s");
  std::printf("  %.1f actions and %.1f turns per game, %lld unfinished, %lld refused actions\n",
              per_game(all.actions), per_game(all.turns), all.unfinished, all.refused);
  std::printf("  wins by seat:");
  for (int s = 0; s < players; ++s) std::printf(" %lld", all.wins[static_cast<std::size_t>(s)]);
  std::printf("\n  checksum %llu\n", static_cast<unsigned long long>(all.checksum));
  std::printf("time %.3f s: %.0f games/s, %.2f M actions/s, %.1f us per game\n", seconds,
              static_cast<double>(all.games) / seconds, static_cast<double>(all.actions) / seconds / 1e6,
              seconds / static_cast<double>(all.games) * 1e6);
  // A refused action means the player or the engine is broken.
  return all.refused == 0 ? 0 : 1;
}
