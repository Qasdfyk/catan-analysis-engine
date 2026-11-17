// catan_selfplay: plays whole games between bots and reports how fast the
// engine runs them. With random players this is the "games per second" of
// docs/performance.md; with zoo bots it is what one simulation of the review
// engine costs.
//
//   build/release/tools/catan_selfplay --games 20000
//   build/release/tools/catan_selfplay --games 200000 --threads 16
//   build/release/tools/catan_selfplay --bots zoo --games 2000 --threads 16
//
// --bots random  the random player (random_player.hpp)
// --bots zoo     every seat a new zoo bot each game: archetype, personality
//                and weight seed drawn from the game's random stream
// --bots hard    four hard bots
//
// Game i always uses the random stream of (seed, i), so a seed gives the same
// games, and the same checksum, whatever the number of threads.

#include "catan/random_player.hpp"
#include "catan/zoo.hpp"

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
  // Zoo games: seats played and games won by each archetype and personality
  std::array<long long, cae::kArchetypeCount> archetype_seats{};
  std::array<long long, cae::kArchetypeCount> archetype_wins{};
  std::array<long long, cae::kPersonalityCount> personality_seats{};
  std::array<long long, cae::kPersonalityCount> personality_wins{};
};

enum class Bots { kRandom, kZoo, kHard };

void play_range(int first, int last, int players, std::uint64_t seed, Bots bots, Totals& out) {
  for (int i = first; i < last; ++i) {
    // Seed and game number go into separate halves; splitmix64 inside Rng
    // turns neighboring seeds into unrelated streams.
    cae::Rng rng((seed << 32) ^ static_cast<std::uint64_t>(i));
    cae::PlayoutResult r;
    cae::Game game;
    if (bots == Bots::kRandom) {
      game = cae::random_game(rng, players);
      r = cae::play_out(game, rng);
    } else {
      cae::ZooTable table;
      table.game = cae::random_game(rng, players);
      for (int s = 0; s < players; ++s) {
        table.bots[static_cast<std::size_t>(s)] = bots == Bots::kZoo ? cae::random_zoo_bot(rng) : cae::hard_bot();
      }
      const cae::ZooPlayout z = cae::play_out_zoo(table, rng);
      game = table.game;
      r = {z.winner, z.actions, z.fallbacks};
      for (int s = 0; s < players; ++s) {
        const cae::ZooBot& bot = table.bots[static_cast<std::size_t>(s)];
        const auto a = static_cast<std::size_t>(bot.archetype);
        const auto p = static_cast<std::size_t>(bot.personality);
        ++out.archetype_seats[a];
        ++out.personality_seats[p];
        if (s == z.winner) {
          ++out.archetype_wins[a];
          ++out.personality_wins[p];
        }
      }
    }
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
  std::fprintf(stderr, "usage: %s [--games N] [--players 2..4] [--seed S] [--threads T] [--bots random|zoo|hard]\n",
               argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  int games = 10'000;
  int players = 4;
  std::uint64_t seed = 1;
  int threads = 1;
  Bots bots = Bots::kRandom;
  std::string bots_name = "random";
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
    } else if (flag == "--bots") {
      bots_name = value;
      if (bots_name == "random") {
        bots = Bots::kRandom;
      } else if (bots_name == "zoo") {
        bots = Bots::kZoo;
      } else if (bots_name == "hard") {
        bots = Bots::kHard;
      } else {
        usage(argv[0]);
      }
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
    pool.emplace_back(play_range, first, last, players, seed, bots, std::ref(per_thread[static_cast<std::size_t>(t)]));
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
    for (std::size_t k = 0; k < all.archetype_seats.size(); ++k) {
      all.archetype_seats[k] += t.archetype_seats[k];
      all.archetype_wins[k] += t.archetype_wins[k];
      all.personality_seats[k] += t.personality_seats[k];
      all.personality_wins[k] += t.personality_wins[k];
    }
  }

  const auto per_game = [&](long long n) { return static_cast<double>(n) / static_cast<double>(all.games); };
  std::printf("%lld games, %s bots, %d players, seed %llu, %d thread%s\n", all.games, bots_name.c_str(), players,
              static_cast<unsigned long long>(seed), threads, threads == 1 ? "" : "s");
  std::printf("  %.1f actions and %.1f turns per game, %lld unfinished, %lld refused actions\n",
              per_game(all.actions), per_game(all.turns), all.unfinished, all.refused);
  if (bots == Bots::kZoo) {
    // Wins per seat played, so archetypes drawn more often do not look stronger
    const auto rate = [](long long wins, long long seats) {
      return seats == 0 ? 0.0 : 100.0 * static_cast<double>(wins) / static_cast<double>(seats);
    };
    std::printf("  win rate per seat by archetype:");
    for (int a = 0; a < cae::kArchetypeCount; ++a) {
      std::printf(" %s %.1f%%", std::string(cae::archetype_name(static_cast<cae::Archetype>(a))).c_str(),
                  rate(all.archetype_wins[static_cast<std::size_t>(a)], all.archetype_seats[static_cast<std::size_t>(a)]));
    }
    std::printf("\n  win rate per seat by personality:");
    for (int p = 0; p < cae::kPersonalityCount; ++p) {
      std::printf(" %s %.1f%%", std::string(cae::personality_name(static_cast<cae::Personality>(p))).c_str(),
                  rate(all.personality_wins[static_cast<std::size_t>(p)],
                       all.personality_seats[static_cast<std::size_t>(p)]));
    }
    std::printf("\n");
  }
  std::printf("  wins by seat:");
  for (int s = 0; s < players; ++s) std::printf(" %lld", all.wins[static_cast<std::size_t>(s)]);
  std::printf("\n  checksum %llu\n", static_cast<unsigned long long>(all.checksum));
  std::printf("time %.3f s: %.0f games/s, %.2f M actions/s, %.1f us per game\n", seconds,
              static_cast<double>(all.games) / seconds, static_cast<double>(all.actions) / seconds / 1e6,
              seconds / static_cast<double>(all.games) * 1e6);
  // A refused action (for zoo bots, a fallback) means a player or the engine
  // is broken.
  return all.refused == 0 ? 0 : 1;
}
