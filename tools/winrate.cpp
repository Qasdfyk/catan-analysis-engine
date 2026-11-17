// catan_winrate: chances to win along a recorded game, by Monte Carlo.
//
// A first sketch of step 10. At the start of every turn of a record, the
// position is played out to the end many times by zoo bots (zoo.hpp), and
// the share of playouts each seat wins is its chance to win from there.
//
//   build/release/tools/catan_winrate --playouts 400 --threads 16 game.json
//   build/release/tools/catan_winrate --playouts 200 --threads 16 records/*.json
//
// With several records it also checks calibration: of all the positions
// where it said "30%", the player should have won about 30% of the games.
//
// What a playout keeps from the position and what it draws anew:
//   kept   the board, buildings, roads, robber, every hand, development
//          cards already held, points, awards, whose turn it is
//   drawn  the dice, stolen cards, the order of the undrawn development
//          cards (nobody knows it), and the opponents: every seat is a new
//          zoo bot each playout (--bots zoo), or the bot that sat there
//          (--bots record, for games the zoo played)
// Hands and held cards are those of the record, so this is a spectator's
// view. A player's own view, with the hidden cards drawn from what they
// could know, comes with the move review (step 13 of the plan).

#include "catan/record.hpp"
#include "catan/zoo.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

struct Options {
  int playouts = 200;
  int threads = 1;
  std::uint64_t seed = 1;
  bool record_bots = false;
  bool quiet = false;  // only the summary
};

struct Checkpoint {
  int event = 0;  // the position before this event
  cae::Game game;
};

struct Chances {
  std::array<double, cae::kMaxPlayers> win{};
  double unfinished = 0;
};

// Fisher-Yates over the cards not drawn yet.
void shuffle_undrawn(cae::Game& game, cae::Rng& rng) {
  for (int i = game.dev_deck_size - 1; i > game.dev_deck_next; --i) {
    const int j = game.dev_deck_next + rng.below(i - game.dev_deck_next + 1);
    std::swap(game.dev_deck[at(i)], game.dev_deck[at(j)]);
  }
}

Chances estimate(const cae::Game& start, const std::array<std::optional<cae::ZooBot>, cae::kMaxPlayers>& recorded,
                 int checkpoint, const Options& o) {
  std::vector<std::array<int, cae::kMaxPlayers + 1>> per_thread(at(o.threads));
  std::vector<std::thread> pool;
  for (int t = 0; t < o.threads; ++t) {
    pool.emplace_back([&, t] {
      auto& wins = per_thread[at(t)];
      for (int p = t; p < o.playouts; p += o.threads) {
        // Playout p of checkpoint c has its own stream, whatever the threads
        cae::Rng rng(o.seed * 0x9e3779b97f4a7c15ULL ^ (static_cast<std::uint64_t>(checkpoint) << 32) ^
                     static_cast<std::uint64_t>(p));
        cae::ZooTable table;
        table.game = start;
        shuffle_undrawn(table.game, rng);
        for (int s = 0; s < start.player_count; ++s) {
          const auto& bot = recorded[at(s)];
          table.bots[at(s)] = o.record_bots && bot ? *bot : cae::random_zoo_bot(rng);
        }
        const cae::ZooPlayout r = cae::play_out_zoo(table, rng);
        ++wins[at(r.winner < 0 ? cae::kMaxPlayers : r.winner)];
      }
    });
  }
  for (auto& th : pool) th.join();
  Chances c;
  for (const auto& wins : per_thread) {
    for (int s = 0; s < cae::kMaxPlayers; ++s) c.win[at(s)] += wins[at(s)];
    c.unfinished += wins[cae::kMaxPlayers];
  }
  for (auto& w : c.win) w /= o.playouts;
  c.unfinished /= o.playouts;
  return c;
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--playouts N] [--threads T] [--seed S] [--bots zoo|record] [--quiet] RECORD.json...\n",
               argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    const auto value = [&]() -> const char* {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--playouts") {
      o.playouts = std::atoi(value());
    } else if (flag == "--threads") {
      o.threads = std::atoi(value());
    } else if (flag == "--seed") {
      o.seed = std::strtoull(value(), nullptr, 10);
    } else if (flag == "--bots") {
      const std::string_view b = value();
      if (b != "zoo" && b != "record") usage(argv[0]);
      o.record_bots = b == "record";
    } else if (flag == "--quiet") {
      o.quiet = true;
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else {
      files.emplace_back(flag);
    }
  }
  if (files.empty() || o.playouts < 1 || o.threads < 1) usage(argv[0]);

  // Calibration: predictions in 10 buckets by chance, and how often the
  // player then actually won. Brier score: mean squared error of the chance.
  constexpr int kBuckets = 10;
  std::array<long, kBuckets> said{};
  std::array<long, kBuckets> won{};
  std::array<double, kBuckets> sum_chance{};
  double brier = 0;
  long predictions = 0;
  long positions = 0;
  long long games_played = 0;
  const auto t0 = std::chrono::steady_clock::now();

  for (const std::string& path : files) {
    json record;
    cae::Game game;
    std::array<std::optional<cae::ZooBot>, cae::kMaxPlayers> bots{};
    try {
      record = cae::read_record_file(path);
      game = cae::game_from_record(record);
      for (const json& player : record["players"]) {
        const int seat = player.value("seat", -1);
        if (seat >= 0 && seat < cae::kMaxPlayers) bots[at(seat)] = cae::zoo_bot_from_record(player);
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
      return 1;
    }

    // The start of every turn after setup: the dice are about to be rolled.
    std::vector<Checkpoint> checkpoints;
    const json& events = record["events"];
    for (std::size_t i = 0; i < events.size(); ++i) {
      const bool turn_start = game.phase == cae::Phase::kRollDice && game.dice_roll == 0 &&
                              (checkpoints.empty() || checkpoints.back().game.turn_number != game.turn_number);
      if (turn_start) checkpoints.push_back({static_cast<int>(i), game});
      if (const cae::Error e = cae::apply_event(game, events[i]); e != cae::Error::kNone) {
        std::fprintf(stderr, "%s: event %zu refused: %s\n", path.c_str(), i, std::string(cae::error_name(e)).c_str());
        return 1;
      }
    }
    const int winner = game.winner;

    if (!o.quiet) {
      std::printf("%s: %zu turns, winner seat %d\n%6s %5s  %-23s %s\n", path.c_str(), checkpoints.size(), winner,
                  "event", "turn", "points", "chance to win");
    }
    for (std::size_t c = 0; c < checkpoints.size(); ++c) {
      const Checkpoint& cp = checkpoints[c];
      const Chances chances = estimate(cp.game, bots, static_cast<int>(c), o);
      games_played += o.playouts;
      ++positions;
      if (!o.quiet) {
        std::string pts;
        std::string pct;
        for (int s = 0; s < cp.game.player_count; ++s) {
          pts += std::to_string(cp.game.visible_points(s)) + (s + 1 < cp.game.player_count ? " " : "");
          char buf[16];
          std::snprintf(buf, sizeof buf, "%5.1f%%", 100 * chances.win[at(s)]);
          pct += buf;
        }
        std::printf("%6d %5d  %-23s %s%s\n", cp.event, cp.game.turn_number, pts.c_str(), pct.c_str(),
                    chances.unfinished > 0 ? "  (some playouts unfinished)" : "");
      }
      if (winner < 0) continue;
      for (int s = 0; s < cp.game.player_count; ++s) {
        const double p = chances.win[at(s)];
        const int outcome = s == winner ? 1 : 0;
        const int b = std::min(kBuckets - 1, static_cast<int>(p * kBuckets));
        ++said[at(b)];
        won[at(b)] += outcome;
        sum_chance[at(b)] += p;
        brier += (p - outcome) * (p - outcome);
        ++predictions;
      }
    }
  }

  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("\n%ld positions in %zu games, %d playouts each (%lld playouts) in %.1f s: %.0f playouts/s\n", positions,
              files.size(), o.playouts, games_played, seconds, static_cast<double>(games_played) / seconds);
  if (predictions > 0) {
    std::printf("\ncalibration: when it said ..., the player won ...\n%10s %10s %10s %8s\n", "said", "mean", "won",
                "count");
    for (int b = 0; b < kBuckets; ++b) {
      if (said[at(b)] == 0) continue;
      std::printf("%3d-%3d%%   %8.1f%% %9.1f%% %8ld\n", b * 10, b * 10 + 10,
                  100 * sum_chance[at(b)] / static_cast<double>(said[at(b)]),
                  100 * static_cast<double>(won[at(b)]) / static_cast<double>(said[at(b)]), said[at(b)]);
    }
    std::printf("Brier score %.4f (always saying 1/players scores %.4f with 4 players)\n",
                brier / static_cast<double>(predictions), 0.1875);
  }
  return 0;
}
