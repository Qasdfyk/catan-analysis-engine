// catan_mcts: what the searching bot (include/catan/mcts.hpp) thinks of the
// decisions of a recorded game.
//
//   build/release/tools/catan_mcts --decisions 8 --iterations 400 tests/data/zoo-game-00088.json
//
// For each of --decisions searched decisions spread over the game (0 = all)
// it prints the moves the search looked at most, with their visits and share
// of won playouts, and the move it would play; --decision K searches only the
// K-th of them (0-based), as the Performance page does. --workers W runs each search
// as W independent searches merged at the root (root parallelization), on
// as many threads; W searches of N iterations give the same numbers on any
// number of threads, and in the browser (web/scripts/mcts-node.mjs prints the
// same "mcts checksum").
//
// Playouts seat MAP-Elites from --elites (default data/elites.json) as the
// opponents, like the arena's default; --rollouts hard seats the hard bot.

#include "catan/elites.hpp"
#include "catan/mcts.hpp"
#include "catan/replay.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--decisions K] [--decision I] [--iterations N] [--workers W] [--seed S] [--c C] [--prior P]\n"
               "          [--rollouts hard|elites] [--elites PATH] [--top K] RECORD.json\n",
               argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  int decisions = 8;
  int only = -1;
  int workers = 1;
  int top = 5;
  std::uint64_t seed = 1;
  bool use_elites = true;
  std::string elites_path = "data/elites.json";
  cae::MctsSettings settings;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--decisions") {
      decisions = std::atoi(value().c_str());
    } else if (flag == "--decision") {
      only = std::atoi(value().c_str());
    } else if (flag == "--iterations") {
      settings.iterations = std::max(1, std::atoi(value().c_str()));
    } else if (flag == "--workers") {
      workers = std::max(1, std::atoi(value().c_str()));
    } else if (flag == "--seed") {
      seed = std::stoull(value());
    } else if (flag == "--c") {
      settings.exploration = std::atof(value().c_str());
    } else if (flag == "--prior") {
      settings.zoo_prior = std::atof(value().c_str());
    } else if (flag == "--rollouts") {
      const std::string r = value();
      if (r != "hard" && r != "elites") usage(argv[0]);
      use_elites = r == "elites";
    } else if (flag == "--elites") {
      elites_path = value();
    } else if (flag == "--top") {
      top = std::max(1, std::atoi(value().c_str()));
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else if (path.empty()) {
      path = flag;
    } else {
      usage(argv[0]);
    }
  }
  if (path.empty()) usage(argv[0]);

  try {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::stringstream text;
    text << in.rdbuf();
    const cae::ReplayLog log = cae::ReplayLog::from_text(text.str());
    if (!log.error().empty()) throw std::runtime_error(path + ": " + log.error());
    const std::vector<cae::ZooBot> pool = use_elites ? cae::load_elites(elites_path) : std::vector<cae::ZooBot>{};
    const std::vector<cae::RecordDecision> list = cae::spread_decisions(log.states(), decisions);

    std::printf("%s: %zu decisions, %d iterations x %d workers each, seed %llu, playouts against %s\n", path.c_str(),
                list.size(), settings.iterations, workers, static_cast<unsigned long long>(seed),
                use_elites ? "elites" : "hard");
    std::uint64_t checksum = 0;
    long long iterations = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t k = 0; k < list.size(); ++k) {
      if (only >= 0 && static_cast<std::size_t>(only) != k) continue;
      const cae::RecordDecision& d = list[k];
      const cae::Game& state = log.state(d.state);
      std::vector<cae::MctsSearch> parts(at(workers));
      std::vector<std::thread> threads;
      for (int w = 0; w < workers; ++w) {
        threads.emplace_back([&, w] { parts[at(w)] = cae::search_part(state, d, pool, settings, seed, w); });
      }
      for (auto& t : threads) t.join();
      cae::MctsSearch s = parts.front();
      for (int w = 1; w < workers; ++w) cae::merge_searches(s, parts[at(w)]);
      iterations += s.iterations;
      for (std::size_t m = 0; m < s.root.size(); ++m) {
        const cae::MctsMove& mv = s.root[m];
        checksum += (static_cast<std::uint64_t>(mv.visits) * 1000 + static_cast<std::uint64_t>(mv.wins)) * (m + 1) *
                    (k + 1);
      }
      std::vector<cae::MctsMove> order = s.root;
      std::sort(order.begin(), order.end(), [](const cae::MctsMove& a, const cae::MctsMove& b) {
        return a.visits > b.visits;
      });
      std::printf("state %d, turn %d, seat %d, %s: %zu moves; plays %s\n", d.state, state.turn_number, d.seat,
                  std::string(cae::phase_name(state.phase)).c_str(), s.root.size(), cae::action_text(s.best).c_str());
      for (std::size_t m = 0; m < order.size() && static_cast<int>(m) < top; ++m) {
        const cae::MctsMove& mv = order[m];
        std::printf("  %-36s %6d visits  %5.1f%% won  prior %.2f\n", cae::action_text(mv.action).c_str(), mv.visits,
                    mv.visits > 0 ? 100.0 * mv.wins / mv.visits : 0.0, mv.prior);
      }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("mcts checksum %llu\n", static_cast<unsigned long long>(checksum));
    std::printf("%lld iterations in %.1f s on %d threads: %.0f iterations/s\n", iterations, seconds, workers,
                static_cast<double>(iterations) / seconds);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
