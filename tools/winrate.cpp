// catan_winrate: chances to win along recorded games, by Monte Carlo.
//
// One game: the chance of every seat before and after each roll, and how
// much of each seat's swing came from the dice (luck) and how much from the
// moves in between (decisions). See include/catan/winrate.hpp.
//
//   build/release/tools/catan_winrate --playouts 400 --threads 16 game.json
//
// Many games: a calibration test. When the estimate says "30%", does the
// player win about 30% of such games? A few positions are drawn from each
// game, and the confidence intervals come from a bootstrap over games, not
// positions: positions of one game share its winner, so they are not
// independent, and treating them as if they were would make the intervals
// too narrow.
//
//   build/release/tools/catan_winrate --calibrate 4 --playouts 200 --threads 16 --bots record records/*.json
//
// --bots zoo (default) seats a new random zoo bot at every seat of every
// playout: what the web page does, since it cannot know who played. --bots
// record seats the bot that played the game (zoo and hard games of the
// platform's tournaments record it), so playouts are played by the same
// players as the real game.

#include "catan/calibration.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"
#include "catan/winrate.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
  int calibrate = 0;  // positions per game; 0 = show each game
  int bootstrap = 1000;
};

// One position to estimate, and who plays it out.
struct Position {
  const cae::Game* game;
  const cae::SeatBots* bots;
};

// All playouts of many positions. The work is cut into chunks of 50
// playouts that the threads take one by one from a shared counter, so no
// thread idles while another still has a long position to finish. The
// chunks add up to exactly what one thread would get (winrate.hpp).
std::vector<cae::WinCounts> estimate(const std::vector<Position>& positions, const Options& o) {
  constexpr int kChunk = 50;
  const int chunks_per_position = (o.playouts + kChunk - 1) / kChunk;
  const int jobs = static_cast<int>(positions.size()) * chunks_per_position;
  std::vector<cae::WinCounts> part(at(jobs));
  std::atomic<int> next{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < o.threads; ++t) {
    pool.emplace_back([&] {
      for (int j = next++; j < jobs; j = next++) {
        const Position& p = positions[at(j / chunks_per_position)];
        const int first = (j % chunks_per_position) * kChunk;
        part[at(j)] = cae::play_out_chances(*p.game, first, std::min(kChunk, o.playouts - first), o.seed, *p.bots);
      }
    });
  }
  for (auto& th : pool) th.join();
  std::vector<cae::WinCounts> out(positions.size());
  for (int j = 0; j < jobs; ++j) out[at(j / chunks_per_position)] += part[at(j)];
  return out;
}

struct Game {
  std::string path;
  cae::ReplayLog log;
  cae::SeatBots bots{};
  std::vector<cae::ChancePoint> points;
};

bool load(const std::string& path, const Options& o, Game& out) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  if (!in) {
    std::fprintf(stderr, "%s: cannot read\n", path.c_str());
    return false;
  }
  out.path = path;
  out.log = cae::ReplayLog::from_text(text.str());
  if (!out.log.error().empty()) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), out.log.error().c_str());
    return false;
  }
  if (o.record_bots) {
    const json record = json::parse(text.str());
    for (const json& player : record["players"]) {
      const int seat = player.value("seat", -1);
      if (seat >= 0 && seat < cae::kMaxPlayers) out.bots[at(seat)] = cae::zoo_bot_from_record(player);
    }
    for (int s = 0; s < out.log.states().front().player_count; ++s) {
      if (!out.bots[at(s)]) {
        std::fprintf(stderr, "%s: seat %d has no zoo bot in the record; use --bots zoo\n", path.c_str(), s);
        return false;
      }
    }
  }
  out.points = cae::chance_points(out.log.states());
  return true;
}

const char* kind_name(cae::PointKind k) {
  switch (k) {
    case cae::PointKind::kStart: return "start";
    case cae::PointKind::kBeforeRoll: return "before roll";
    case cae::PointKind::kAfterRoll: return "after roll";
    case cae::PointKind::kEnd: return "end";
  }
  return "?";
}

// --- One game ----------------------------------------------------------------

void show_game(const Game& g, const Options& o) {
  const int players = g.log.states().front().player_count;
  std::printf("%s: %zu points, winner seat %d\n%6s %5s %-12s %-12s %s\n", g.path.c_str(), g.points.size(),
              g.log.states().back().winner, "state", "turn", "point", "points", "chance to win");
  std::vector<std::array<double, cae::kMaxPlayers>> chance;
  // Every win of every point, folded together; web/scripts/winrate-node.mjs
  // prints the same number from the WebAssembly build.
  std::uint64_t checksum = 0;
  std::vector<Position> positions;
  for (const cae::ChancePoint& p : g.points) positions.push_back({&g.log.state(p.state), &g.bots});
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<cae::WinCounts> counts = estimate(positions, o);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  for (std::size_t i = 0; i < g.points.size(); ++i) {
    const cae::ChancePoint& p = g.points[i];
    const cae::Game& state = g.log.state(p.state);
    const cae::WinCounts& w = counts[i];
    for (int s = 0; s < players; ++s) checksum += static_cast<std::uint64_t>(w.wins[at(s)]) * (i + 1) * at(s + 1);
    std::array<double, cae::kMaxPlayers> c{};
    std::string pts;
    std::string pct;
    for (int s = 0; s < players; ++s) {
      c[at(s)] = w.share(s);
      pts += std::to_string(state.visible_points(s)) + (s + 1 < players ? " " : "");
      char buf[16];
      std::snprintf(buf, sizeof buf, "%6.1f%%", 100 * c[at(s)]);
      pct += buf;
    }
    chance.push_back(c);
    std::printf("%6d %5d %-12s %-12s %s%s\n", p.state, p.turn, kind_name(p.kind), pts.c_str(), pct.c_str(),
                w.unfinished > 0 ? "  (some playouts unfinished)" : "");
  }

  // Luck: across each roll. Decisions: everything else, setup included.
  std::array<double, cae::kMaxPlayers> luck{};
  std::array<double, cae::kMaxPlayers> decisions{};
  for (std::size_t i = 1; i < g.points.size(); ++i) {
    const bool roll = g.points[i].kind == cae::PointKind::kAfterRoll;
    for (int s = 0; s < players; ++s) {
      const double d = chance[i][at(s)] - chance[i - 1][at(s)];
      (roll ? luck : decisions)[at(s)] += d;
    }
  }
  std::printf("\n%-6s %8s %8s %10s %8s\n", "seat", "start", "luck", "decisions", "end");
  for (int s = 0; s < players; ++s) {
    std::printf("%-6d %7.1f%% %+7.1f%% %+9.1f%% %7.1f%%\n", s, 100 * chance.front()[at(s)], 100 * luck[at(s)],
                100 * decisions[at(s)], 100 * chance.back()[at(s)]);
  }
  std::printf("(in percentage points; start + luck + decisions = end)\nwins checksum %llu\n",
              static_cast<unsigned long long>(checksum));
  const double total = static_cast<double>(g.points.size()) * o.playouts;
  std::printf("%.0f playouts in %.2f s on %d threads: %.0f playouts/s\n\n", total, seconds, o.threads, total / seconds);
}

// --- Calibration -------------------------------------------------------------

void calibrate(const std::vector<Game>& games, const Options& o) {
  // Positions: `calibrate` turns per game, drawn at random among the points
  // just before a roll, so early, middle and late game all show up.
  struct Picked {
    int game;
    int state;
  };
  std::vector<Picked> picked;
  std::vector<Position> todo;
  for (std::size_t gi = 0; gi < games.size(); ++gi) {
    const Game& g = games[gi];
    if (g.log.states().back().winner < 0) continue;  // unfinished: no outcome to compare with
    std::vector<int> candidates;
    for (const auto& p : g.points) {
      if (p.kind == cae::PointKind::kBeforeRoll) candidates.push_back(p.state);
    }
    cae::Rng pick(o.seed ^ (static_cast<std::uint64_t>(gi) << 20));
    for (int i = static_cast<int>(candidates.size()) - 1; i > 0; --i) {
      std::swap(candidates[at(i)], candidates[at(pick.below(i + 1))]);
    }
    candidates.resize(std::min(candidates.size(), at(o.calibrate)));
    for (const int state_index : candidates) {
      picked.push_back({static_cast<int>(gi), state_index});
      todo.push_back({&g.log.state(state_index), &g.bots});
    }
  }
  // Every position of every game at once, so the threads stay busy.
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<cae::WinCounts> counts = estimate(todo, o);
  const long positions = static_cast<long>(picked.size());
  const long long playouts = static_cast<long long>(positions) * o.playouts;

  // Predictions grouped by game, for the bootstrap.
  std::vector<std::vector<cae::Prediction>> by_game;
  int last_game = -1;
  for (std::size_t k = 0; k < picked.size(); ++k) {
    const Game& g = games[at(picked[k].game)];
    const int winner = g.log.states().back().winner;
    if (picked[k].game != last_game) by_game.emplace_back();
    last_game = picked[k].game;
    const cae::Game& state = g.log.state(picked[k].state);
    for (int s = 0; s < state.player_count; ++s) by_game.back().push_back({counts[k].share(s), s == winner ? 1 : 0});
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::printf("%d games, %ld positions, %d playouts each (%lld playouts) in %.1f s: %.0f playouts/s\n\n",
              static_cast<int>(by_game.size()), positions, o.playouts, playouts, seconds,
              static_cast<double>(playouts) / seconds);
  cae::print_calibration(cae::calibrate(by_game, o.bootstrap, o.seed + 12345),
                         games.front().log.states().front().player_count);
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--playouts N] [--threads T] [--seed S] [--bots zoo|record]\n"
               "          [--calibrate POSITIONS_PER_GAME] [--bootstrap B] RECORD.json...\n",
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
    } else if (flag == "--calibrate") {
      o.calibrate = std::atoi(value());
    } else if (flag == "--bootstrap") {
      o.bootstrap = std::atoi(value());
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else {
      files.emplace_back(flag);
    }
  }
  if (files.empty() || o.playouts < 1 || o.threads < 1 || o.calibrate < 0) usage(argv[0]);

  std::vector<Game> games(files.size());
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (!load(files[i], o, games[i])) return 1;
  }
  if (o.calibrate > 0) {
    calibrate(games, o);
  } else {
    for (const Game& g : games) show_game(g, o);
  }
  return 0;
}
