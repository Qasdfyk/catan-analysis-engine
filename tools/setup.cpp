// catan_setup: review of the initial placement of recorded games
// (include/catan/setup_review.hpp).
//
// One game: every setup settlement with the corner taken, the best corner,
// what the choice cost in chance to win, and the top corners.
//
//   build/release/tools/catan_setup --threads 16 game.json
//
// Many games (--summary): how much each kind of player loses in the setup,
// how often it takes the best corner, how large the winner's curse of the
// halving would be without the confirmation round, and whether a better
// setup wins more often. Intervals are 95%, from a bootstrap over games.
//
//   build/release/tools/catan_setup --summary --threads 16 records/*.json
//
// Playouts seat MAP-Elites drawn from --elites (default data/elites.json),
// as the web page does; --zoo seats random zoo bots instead.

#include "catan/calibration.hpp"
#include "catan/elites.hpp"
#include "catan/replay.hpp"
#include "catan/setup_review.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

struct Options {
  cae::SetupReviewConfig config;
  int threads = 1;
  std::string elites_path = "data/elites.json";
  bool zoo = false;
  bool summary = false;
  int bootstrap = 1000;
};

struct Game {
  std::string path;
  cae::ReplayLog log;
  std::vector<std::string> who;  // per seat: "human", "hard", "zoo", ...
  std::vector<cae::SetupMove> moves;
};

bool load(const std::string& path, Game& out) {
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
  const json record = json::parse(text.str());
  out.who.assign(at(out.log.state(0).player_count), "?");
  for (const json& p : record["players"]) {
    const int seat = p.value("seat", -1);
    if (seat < 0 || seat >= static_cast<int>(out.who.size())) continue;
    if (p.value("kind", "") == "human") {
      out.who[at(seat)] = "human";
    } else if (p.contains("bot_level") && p["bot_level"].is_string()) {
      out.who[at(seat)] = p["bot_level"].get<std::string>();
    }
  }
  out.moves = cae::setup_moves(out.log.states());
  return true;
}

// What one review leaves behind, small enough to keep for thousands of moves.
struct Result {
  int game = 0;
  int move = 0;  // index into Game::moves
  std::vector<int> nodes;
  std::vector<double> value;
  std::vector<int> value_playouts;
  std::vector<bool> finalist;
  int best = -1;
  int played = -1;
  cae::SetupReview::Loss loss;
  long long playouts = 0;
  std::uint64_t checksum = 0;
};

Result review(const Game& g, int game_index, int move_index, const Options& o, std::span<const cae::ZooBot> pool) {
  const cae::SetupMove& m = g.moves[at(move_index)];
  cae::SetupReview r(g.log.state(m.state), m.node, o.config, pool);
  while (!r.done()) r.step();
  Result out;
  out.game = game_index;
  out.move = move_index;
  out.best = r.best();
  out.played = r.played();
  out.loss = r.loss();
  out.playouts = r.playouts();
  // Every win count of every corner, folded together: web/scripts/setup-node.mjs
  // prints the same number from the WebAssembly build.
  for (int c = 0; c < static_cast<int>(r.candidates().size()); ++c) {
    const cae::SetupCandidate& k = r.candidates()[at(c)];
    out.nodes.push_back(k.node);
    out.value.push_back(r.value(c));
    out.value_playouts.push_back(r.value_playouts(c));
    out.finalist.push_back(k.finalist);
    for (int s = 0; s < cae::kMaxPlayers; ++s) {
      const auto halving = static_cast<std::uint64_t>(k.halving.wins[at(s)]);
      const auto confirm = static_cast<std::uint64_t>(k.confirm.wins[at(s)]);
      out.checksum += (halving * 7 + confirm) * at(c + 1) * at(s + 1) * at(move_index + 1);
    }
  }
  return out;
}

// Every move of every game, one review per thread at a time.
std::vector<Result> review_all(const std::vector<Game>& games, const Options& o, std::span<const cae::ZooBot> pool) {
  std::vector<std::pair<int, int>> jobs;
  for (std::size_t g = 0; g < games.size(); ++g) {
    for (std::size_t m = 0; m < games[g].moves.size(); ++m) jobs.emplace_back(static_cast<int>(g), static_cast<int>(m));
  }
  std::vector<Result> out(jobs.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> finished{0};
  std::vector<std::thread> pool_threads;
  for (int t = 0; t < o.threads; ++t) {
    pool_threads.emplace_back([&] {
      for (std::size_t j = next++; j < jobs.size(); j = next++) {
        out[j] = review(games[at(jobs[j].first)], jobs[j].first, jobs[j].second, o, pool);
        const std::size_t f = ++finished;
        if (o.summary && f % 200 == 0) std::fprintf(stderr, "%zu of %zu moves\n", f, jobs.size());
      }
    });
  }
  for (auto& th : pool_threads) th.join();
  return out;
}

double pp(double x) { return 100.0 * x; }

// --- One game ----------------------------------------------------------------

void show_game(const Game& g, const std::vector<Result>& results, double seconds, const Options& o) {
  std::printf("%s: winner seat %d\n", g.path.c_str(), g.log.states().back().winner);
  std::printf("%4s %4s %5s %-7s %6s %8s %6s %8s %16s  %s\n", "move", "seat", "round", "who", "played", "chance",
              "best", "chance", "loss (95%)", "top corners");
  std::uint64_t checksum = 0;
  long long playouts = 0;
  for (const Result& r : results) {
    const cae::SetupMove& m = g.moves[at(r.move)];
    checksum += r.checksum;
    playouts += r.playouts;
    // The finalists by value; the others had fewer playouts.
    std::vector<int> order;
    for (int c = 0; c < static_cast<int>(r.nodes.size()); ++c) {
      if (r.finalist[at(c)]) order.push_back(c);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return r.value[at(a)] > r.value[at(b)]; });
    std::string top;
    for (const int c : order) {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%d:%.1f ", r.nodes[at(c)], pp(r.value[at(c)]));
      top += buf;
    }
    char loss[40];
    std::snprintf(loss, sizeof loss, "%+.1f ± %.1f", pp(r.loss.loss), pp(1.96 * r.loss.error));
    std::printf("%4d %4d %5d %-7s %6d %7.1f%% %6d %7.1f%% %16s  %s\n", r.move, m.seat, m.round + 1,
                g.who[at(m.seat)].c_str(), m.node, pp(r.value[at(r.played)]), r.nodes[at(r.best)],
                pp(r.value[at(r.best)]), loss, top.c_str());
  }
  std::printf("(chances in %%, loss in percentage points; top corners are the finalists, corner:chance)\n");
  std::printf("setup checksum %llu\n", static_cast<unsigned long long>(checksum));
  std::printf("%lld playouts in %.1f s on %d threads: %.0f playouts/s\n\n", playouts, seconds, o.threads,
              static_cast<double>(playouts) / seconds);
}

// --- Many games --------------------------------------------------------------

// A statistic over games and its 95% bootstrap interval: games are drawn with
// replacement, since the moves of one game share its board and its players.
template <typename F>
std::array<double, 3> with_interval(int games, int bootstrap, std::uint64_t seed, F&& stat) {
  std::vector<int> all(at(games));
  for (int i = 0; i < games; ++i) all[at(i)] = i;
  const double value = stat(all);
  cae::Rng rng(seed);
  std::vector<double> draws;
  std::vector<int> pick(at(games));
  for (int b = 0; b < bootstrap; ++b) {
    for (int i = 0; i < games; ++i) pick[at(i)] = rng.below(games);
    draws.push_back(stat(pick));
  }
  return {value, cae::percentile(draws, 0.025), cae::percentile(draws, 0.975)};
}

void summary(const std::vector<Game>& games, const std::vector<Result>& results, double seconds, const Options& o) {
  const int n = static_cast<int>(games.size());
  std::vector<std::vector<const Result*>> by_game(at(n));
  long long playouts = 0;
  for (const Result& r : results) {
    by_game[at(r.game)].push_back(&r);
    playouts += r.playouts;
  }
  std::printf("%d games, %zu setup moves, %lld playouts in %.0f s (%.0f playouts/s on %d threads)\n", n,
              results.size(), playouts, seconds, static_cast<double>(playouts) / seconds, o.threads);
  std::printf("budget per move: %d playouts per corner in round 0, doubling; %d finalists; %d to confirm\n\n",
              o.config.first_playouts, o.config.finalists, o.config.confirm_playouts);

  // Per kind of player: loss per move, how often the best corner, how often
  // within the noise of it.
  std::map<std::string, int> kinds;
  for (const Game& g : games) {
    for (const std::string& w : g.who) ++kinds[w];
  }
  std::printf("%-8s %6s %22s %22s %22s\n", "player", "moves", "mean loss (pp)", "took the best", "within noise");
  for (const auto& [kind, _] : kinds) {
    const auto over = [&](const std::vector<int>& pick, auto&& f) {
      double sum = 0;
      long count = 0;
      for (const int gi : pick) {
        for (const Result* r : by_game[at(gi)]) {
          const cae::SetupMove& m = games[at(gi)].moves[at(r->move)];
          if (games[at(gi)].who[at(m.seat)] != kind) continue;
          sum += f(*r);
          ++count;
        }
      }
      return count > 0 ? sum / static_cast<double>(count) : NAN;
    };
    long moves = 0;
    for (const auto& rs : by_game) {
      for (const Result* r : rs) moves += games[at(r->game)].who[at(games[at(r->game)].moves[at(r->move)].seat)] == kind;
    }
    if (moves == 0) continue;
    const auto loss = with_interval(n, o.bootstrap, 11, [&](const auto& p) { return over(p, [](const Result& r) { return pp(r.loss.loss); }); });
    const auto best = with_interval(n, o.bootstrap, 12, [&](const auto& p) { return over(p, [](const Result& r) { return r.played == r.best ? 100.0 : 0.0; }); });
    const auto noise = with_interval(n, o.bootstrap, 13, [&](const auto& p) {
      return over(p, [](const Result& r) { return r.loss.loss <= 1.96 * r.loss.error ? 100.0 : 0.0; });
    });
    const auto cell = [](const std::array<double, 3>& v, const char* unit) {
      char buf[48];
      std::snprintf(buf, sizeof buf, "%.2f%s [%.2f, %.2f]", v[0], unit, v[1], v[2]);
      return std::string(buf);
    };
    std::printf("%-8s %6ld %22s %22s %22s\n", kind.c_str(), moves, cell(loss, "").c_str(), cell(best, "%").c_str(),
                cell(noise, "%").c_str());
  }

  // The winner's curse: for the moves where the corner played was not the
  // best, the loss from the playouts that chose the best corner against the
  // loss from fresh ones.
  {
    const auto curse = with_interval(n, o.bootstrap, 14, [&](const auto& pick) {
      double sum = 0;
      long count = 0;
      for (const int gi : pick) {
        for (const Result* r : by_game[at(gi)]) {
          if (r->played == r->best) continue;
          sum += pp(r->loss.naive - r->loss.loss);
          ++count;
        }
      }
      return count > 0 ? sum / static_cast<double>(count) : NAN;
    });
    std::printf("\nwinner's curse: the halving's own loss is higher by %.2f pp [%.2f, %.2f] than the confirmed one\n",
                curse[0], curse[1], curse[2]);
  }

  // Does a better setup win more often? Per seat, the loss of both
  // settlements together; within each game, the seat that lost least
  // against the one that lost most; and all seats by quarters of loss.
  struct Seat {
    double loss;
    bool won;
  };
  std::vector<std::vector<Seat>> seats(at(n));
  std::vector<double> all_losses;
  for (int gi = 0; gi < n; ++gi) {
    const Game& g = games[at(gi)];
    const int winner = g.log.states().back().winner;
    if (winner < 0) continue;
    std::vector<double> total(g.who.size(), 0.0);
    for (const Result* r : by_game[at(gi)]) total[at(g.moves[at(r->move)].seat)] += r->loss.loss;
    for (std::size_t s = 0; s < total.size(); ++s) {
      seats[at(gi)].push_back({total[s], static_cast<int>(s) == winner});
      all_losses.push_back(total[s]);
    }
  }
  const auto least = with_interval(n, o.bootstrap, 15, [&](const auto& pick) {
    double won = 0;
    long count = 0;
    for (const int gi : pick) {
      const auto& ss = seats[at(gi)];
      if (ss.empty()) continue;
      const auto it = std::min_element(ss.begin(), ss.end(), [](const Seat& a, const Seat& b) { return a.loss < b.loss; });
      won += it->won ? 1 : 0;
      ++count;
    }
    return count > 0 ? 100.0 * won / static_cast<double>(count) : NAN;
  });
  const auto most = with_interval(n, o.bootstrap, 16, [&](const auto& pick) {
    double won = 0;
    long count = 0;
    for (const int gi : pick) {
      const auto& ss = seats[at(gi)];
      if (ss.empty()) continue;
      const auto it = std::max_element(ss.begin(), ss.end(), [](const Seat& a, const Seat& b) { return a.loss < b.loss; });
      won += it->won ? 1 : 0;
      ++count;
    }
    return count > 0 ? 100.0 * won / static_cast<double>(count) : NAN;
  });
  std::printf("\nsetup and result (seat loss = both settlements, %zu seats):\n", all_losses.size());
  std::printf("  the seat that lost least in its game wins %.1f%% [%.1f, %.1f]\n", least[0], least[1], least[2]);
  std::printf("  the seat that lost most in its game wins  %.1f%% [%.1f, %.1f]\n", most[0], most[1], most[2]);
  std::sort(all_losses.begin(), all_losses.end());
  std::array<double, 3> cut{};
  for (int q = 0; q < 3; ++q) cut[at(q)] = all_losses[all_losses.size() * at(q + 1) / 4];
  const auto quarter = [&](double loss) {
    int q = 0;
    while (q < 3 && loss >= cut[at(q)]) ++q;
    return q;
  };
  std::printf("  %-28s %10s %22s\n", "quarter of seat loss (pp)", "seats", "win rate");
  for (int q = 0; q < 4; ++q) {
    long count = 0;
    for (const auto& ss : seats) {
      for (const Seat& s : ss) count += quarter(s.loss) == q;
    }
    const auto rate = with_interval(n, o.bootstrap, 17 + static_cast<std::uint64_t>(q), [&](const auto& pick) {
      double won = 0;
      long c = 0;
      for (const int gi : pick) {
        for (const Seat& s : seats[at(gi)]) {
          if (quarter(s.loss) != q) continue;
          won += s.won ? 1 : 0;
          ++c;
        }
      }
      return c > 0 ? 100.0 * won / static_cast<double>(c) : NAN;
    });
    char range[48];
    const double lo = q == 0 ? all_losses.front() : cut[at(q - 1)];
    const double hi = q == 3 ? all_losses.back() : cut[at(q)];
    std::snprintf(range, sizeof range, "%+.1f .. %+.1f", pp(lo), pp(hi));
    std::printf("  %-28s %10ld %8.1f%% [%.1f, %.1f]\n", range, count, rate[0], rate[1], rate[2]);
  }
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--threads T] [--seed S] [--first N] [--finalists K] [--confirm N]\n"
               "          [--elites PATH | --zoo] [--summary] [--bootstrap B] RECORD.json...\n",
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
    if (flag == "--threads") {
      o.threads = std::atoi(value());
    } else if (flag == "--seed") {
      o.config.seed = std::strtoull(value(), nullptr, 10);
    } else if (flag == "--first") {
      o.config.first_playouts = std::atoi(value());
    } else if (flag == "--finalists") {
      o.config.finalists = std::atoi(value());
    } else if (flag == "--confirm") {
      o.config.confirm_playouts = std::atoi(value());
    } else if (flag == "--elites") {
      o.elites_path = value();
    } else if (flag == "--zoo") {
      o.zoo = true;
    } else if (flag == "--summary") {
      o.summary = true;
    } else if (flag == "--bootstrap") {
      o.bootstrap = std::atoi(value());
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else {
      files.emplace_back(flag);
    }
  }
  if (files.empty() || o.threads < 1 || o.config.first_playouts < 1 || o.config.finalists < 1 ||
      o.config.confirm_playouts < 1) {
    usage(argv[0]);
  }

  std::vector<cae::ZooBot> elites;
  if (!o.zoo) {
    try {
      elites = cae::load_elites(o.elites_path);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s\n", e.what());
      return 1;
    }
  }
  std::vector<Game> games(files.size());
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (!load(files[i], games[i])) return 1;
  }
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<Result> results = review_all(games, o, elites);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (o.summary) {
    summary(games, results, seconds, o);
  } else {
    for (std::size_t g = 0; g < games.size(); ++g) {
      std::vector<Result> mine;
      for (const Result& r : results) {
        if (r.game == static_cast<int>(g)) mine.push_back(r);
      }
      show_game(games[g], mine, seconds, o);
    }
  }
  return 0;
}
