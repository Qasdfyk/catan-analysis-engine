// catan_golden: builds the golden positions (set B, include/catan/golden.hpp)
// from game records of the platform.
//
//   build/release/tools/catan_golden --per-game 2 --threads 16 --out data/golden-b.jsonl RECORD.json...
//
// From each record it takes --per-game decisions the searching bot would
// search (record_decisions in mcts.hpp), one drawn from each equal part of
// the game, so the set covers the setup, the middle and the end. Each one is
// labelled by label_position() and written as one line of JSON:
//
//   {"id": "game-00012-ab12cd34#412", "state": 412, "seat": 2, "turn": 31,
//    "phase": "main", "board": {...}, "position": {...}, "known": [[...]],
//    "memory": {...}, "played": 3, "best": 0,
//    "moves": [{"move": {...}, "value": 0.312, "playouts": 2400,
//               "halving_value": 0.305, "halving_playouts": 896}, ...]}
//
// "position" is the true state (snapshot() in record.hpp), hidden cards
// included: a reader that grades moves must search with "known" only, as
// the label did. Values are chances to win of the seat. Positions are
// labelled in parallel, one per thread, and the file does not depend on
// the number of threads.

#include "catan/elites.hpp"
#include "catan/expert.hpp"
#include "catan/golden.hpp"
#include "catan/mcts.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"
#include "catan/rng.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  return a * 0x9e3779b97f4a7c15ULL + b + 0x632be59bd9b4e019ULL;
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--per-game K] [--threads N] [--first N] [--finalists N] [--confirm N] [--seed S]\n"
               "          [--elites PATH] [--limit N] --out FILE RECORD.json...\n",
               argv0);
  std::exit(2);
}

struct Job {
  std::string id;
  json board;
  cae::Game game;
  int state = 0;
  int seat = 0;
  std::array<cae::Cards, cae::kMaxPlayers> known{};
  cae::TableMemory memory;
  std::optional<cae::Action> played;
};

std::string phase_of(const cae::Game& g) {
  if (g.phase == cae::Phase::kSetup) return "setup";
  if (g.phase == cae::Phase::kMoveRobber || g.phase == cae::Phase::kSteal) return "robber";
  return std::string(cae::phase_name(g.phase));
}

json line_of(const Job& job, const cae::GoldenLabel& label) {
  json moves = json::array();
  for (const cae::GoldenMove& m : label.moves) {
    json one = {{"move", cae::action_json(m.action)},
                {"halving_value", m.halving.share(job.seat)},
                {"halving_playouts", m.halving.playouts}};
    if (m.confirm.playouts > 0) {
      one["value"] = m.confirm.share(job.seat);
      one["playouts"] = m.confirm.playouts;
    }
    moves.push_back(one);
  }
  json known = json::array();
  for (const cae::Cards& c : job.known) known.push_back(std::vector<int>(c.begin(), c.end()));
  return {{"id", job.id},
          {"state", job.state},
          {"seat", job.seat},
          {"turn", job.game.turn_number},
          {"phase", phase_of(job.game)},
          {"board", job.board},
          {"position", cae::snapshot(job.game)},
          {"known", known},
          {"memory", cae::memory_json(job.memory)},
          {"played", label.played},
          {"best", label.best},
          {"moves", moves}};
}

}  // namespace

int main(int argc, char** argv) {
  int per_game = 2;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  int limit = 0;
  std::string out_path;
  std::string elites_path = "data/elites.json";
  cae::GoldenConfig config;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--per-game") {
      per_game = std::atoi(value().c_str());
    } else if (flag == "--threads") {
      threads = std::atoi(value().c_str());
    } else if (flag == "--first") {
      config.first_playouts = std::atoi(value().c_str());
    } else if (flag == "--finalists") {
      config.finalists = std::atoi(value().c_str());
    } else if (flag == "--confirm") {
      config.confirm_playouts = std::atoi(value().c_str());
    } else if (flag == "--seed") {
      config.seed = std::strtoull(value().c_str(), nullptr, 10);
    } else if (flag == "--elites") {
      elites_path = value();
    } else if (flag == "--limit") {
      limit = std::atoi(value().c_str());
    } else if (flag == "--out") {
      out_path = value();
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else {
      paths.push_back(flag);
    }
  }
  if (out_path.empty() || paths.empty() || per_game < 1 || threads < 1) usage(argv[0]);

  try {
    const std::vector<cae::ZooBot> pool = cae::load_elites(elites_path);
    std::vector<Job> jobs;
    for (std::size_t f = 0; f < paths.size(); ++f) {
      const json record = cae::read_record_file(paths[f]);
      const cae::ReplayLog log = cae::ReplayLog::from_text(record.dump());
      if (!log.error().empty()) {
        std::fprintf(stderr, "%s: skipped, %s\n", paths[f].c_str(), log.error().c_str());
        continue;
      }
      const std::vector<cae::RecordDecision> all = cae::record_decisions(log.states());
      if (all.empty()) continue;
      cae::Rng rng(mix(config.seed, f));
      const std::string name = std::filesystem::path(paths[f]).stem().string();
      for (int k = 0; k < per_game; ++k) {
        // One decision from the k-th of per_game equal parts of the game.
        const int lo = static_cast<int>(all.size()) * k / per_game;
        const int hi = static_cast<int>(all.size()) * (k + 1) / per_game;
        if (hi <= lo) continue;
        const cae::RecordDecision& d = all[at(lo + rng.below(hi - lo))];
        json cut = record;
        cut["events"] = json(std::vector<json>(record["events"].begin(), record["events"].begin() + d.state));
        const cae::TablePosition table = cae::table_from_record(cut);
        Job job;
        job.id = name + "#" + std::to_string(d.state);
        job.board = cae::board_json(table.game.board);
        job.game = table.game;
        job.state = d.state;
        job.seat = d.seat;
        job.known = table.belief.known[at(d.seat)];
        job.memory = table.memory;
        const json& event = record["events"][at(d.state)];
        if (event.value("seat", -1) == d.seat) job.played = cae::action_from_event(event);
        jobs.push_back(std::move(job));
      }
      if (limit > 0 && static_cast<int>(jobs.size()) >= limit) {
        jobs.resize(at(limit));
        break;
      }
    }

    std::fprintf(stderr, "%zu positions from %zu records, %d threads\n", jobs.size(), paths.size(), threads);
    std::vector<std::string> lines(jobs.size());
    std::atomic<int> next{0};
    std::atomic<long long> playouts{0};
    std::atomic<int> finished{0};
    std::mutex print;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (int w = 0; w < threads; ++w) {
      workers.emplace_back([&] {
        for (int i = next++; i < static_cast<int>(jobs.size()); i = next++) {
          const Job& job = jobs[at(i)];
          const cae::GoldenLabel label =
              cae::label_position(job.game, job.seat, job.known, job.memory, job.played, config, pool);
          lines[at(i)] = line_of(job, label).dump();
          playouts += label.playouts;
          const int n = ++finished;
          if (n % 50 == 0) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const std::lock_guard<std::mutex> guard(print);
            std::fprintf(stderr, "%d/%zu positions, %.0f s\n", n, jobs.size(), s);
          }
        }
      });
    }
    for (std::thread& t : workers) t.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::ofstream out(out_path);
    if (!out) throw std::runtime_error("cannot write " + out_path);
    for (const std::string& l : lines) out << l << "\n";
    std::printf("%zu positions, %lld playouts in %.0f s (%.0f playouts/s)\n", jobs.size(),
                static_cast<long long>(playouts), seconds, static_cast<double>(playouts) / seconds);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
