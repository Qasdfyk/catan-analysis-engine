// catan_review: the move review (include/catan/review.hpp) of recorded games.
//
//   build/release/tools/catan_review [--seat N] RECORD.json...
//       grades every decision of seat N (default: every seat) and prints, per
//       player level ("hard" from a player named "hard 2"), the decisions,
//       the grades and the mean loss; --out FILE writes one JSON line per
//       decision
//
//   build/release/tools/catan_review --golden golden-b.jsonl [--limit N]
//       grades the decisions of the golden positions (set B, golden.hpp) the
//       way a review would, and compares with their deep labels: how close
//       the losses are and how often the grades agree
//
// Decisions run in parallel, one per thread; the output does not depend on
// the number of threads.

#include "catan/elites.hpp"
#include "catan/golden.hpp"
#include "catan/record.hpp"
#include "catan/review.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--seat N] [--threads N] [--first N] [--finalists N] [--confirm N] [--seed S]\n"
               "          [--elites PATH] [--setup] [--out FILE] RECORD.json...\n"
               "       %s --golden FILE [--plant] [--limit N] [options above]\n",
               argv0, argv0);
  std::exit(2);
}

struct Job {
  std::string game;   // file name of the record
  std::string level;  // the player's name up to the first space
  cae::ReviewDecision decision;
  cae::MoveReview review;
  // From a golden position: its deep label.
  double golden_loss = 0;
  bool golden_best = false;
  double golden_error = 0;
};

std::vector<std::string> read_lines(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty()) lines.push_back(line);
  }
  return lines;
}

// A golden position as a decision to review, and its deep loss. With
// `plant`, the move played is replaced by the worst move of the label (by the
// halving, which every move has): a planted blunder.
Job golden_job(const json& g, bool plant) {
  Job job;
  job.game = g.at("id").get<std::string>();
  job.level = "golden";
  cae::ReviewDecision& d = job.decision;
  d.state = g.at("state").get<int>();
  d.seat = g.at("seat").get<int>();
  d.game = cae::game_from_snapshot(g.at("board"), g.at("position"));
  for (std::size_t s = 0; s < d.known.size(); ++s) {
    for (std::size_t r = 0; r < d.known[s].size(); ++r) d.known[s][r] = g.at("known").at(s).at(r).get<std::int8_t>();
  }
  d.memory = cae::memory_from_json(g.at("memory"));
  int played = g.at("played").get<int>();
  const int best = g.at("best").get<int>();
  const json& moves = g.at("moves");
  if (plant) {
    for (std::size_t i = 0; i < moves.size(); ++i) {
      if (moves[i].at("halving_value").get<double>() < moves.at(at(played)).at("halving_value").get<double>()) {
        played = static_cast<int>(i);
      }
    }
  }
  d.played = *cae::action_from_json(moves.at(at(played)).at("move"));
  job.golden_best = played == best;
  if (!job.golden_best) {
    const json& b = moves.at(at(best));
    const json& p = moves.at(at(played));
    // A planted move has no confirmation in the label; its halving stands in.
    const auto value = [](const json& m) {
      return m.contains("value") ? m.at("value").get<double>() : m.at("halving_value").get<double>();
    };
    const auto var = [&](const json& m) {
      const double v = value(m);
      return v * (1 - v) /
             (m.contains("playouts") ? m.at("playouts").get<double>() : m.at("halving_playouts").get<double>());
    };
    job.golden_loss = value(b) - value(p);
    job.golden_error = std::sqrt(var(b) + var(p));
  }
  return job;
}

void print_summary(const std::vector<Job>& jobs, const cae::GradeThresholds& t) {
  std::map<std::string, std::vector<cae::MoveReview>> by_level;
  std::map<std::string, std::map<std::string, int>> games;  // level -> game -> 1
  for (const Job& j : jobs) {
    by_level[j.level].push_back(j.review);
    games[j.level][j.game + "#" + std::to_string(j.decision.seat)] = 1;
  }
  std::printf("%-10s %8s %6s %6s %6s %6s %6s %6s %9s %11s %9s\n", "level", "players", "moves", "best", "good",
              "inacc", "mist", "blund", "loss pp", "blund/game", "noise %");
  for (const auto& [level, reviews] : by_level) {
    const cae::ReviewSummary s = cae::summarize(reviews);
    int noise = 0;
    for (const cae::MoveReview& r : reviews) noise += r.within_noise ? 1 : 0;
    const auto n = static_cast<double>(games[level].size());
    std::printf("%-10s %8zu %6d %6d %6d %6d %6d %6d %9.2f %11.2f %9.1f\n", level.c_str(), games[level].size(),
                s.decisions, s.grades[0], s.grades[1], s.grades[2], s.grades[3], s.grades[4], 100 * s.mean_loss,
                s.grades[4] / n, 100.0 * noise / std::max(1, s.decisions));
  }
  (void)t;
}

void print_golden(const std::vector<Job>& jobs, const cae::GradeThresholds& t) {
  // Grades of the deep label (with its own error) against the review's.
  std::array<std::array<int, cae::kGradeCount>, cae::kGradeCount> table{};
  double abs_diff = 0;
  double diff = 0;
  double paired = 0;
  double unpaired = 0;
  int graded = 0;
  int agree = 0;
  for (const Job& j : jobs) {
    const cae::Grade deep = cae::grade_of(j.golden_loss, j.golden_error, j.golden_best, t);
    const cae::Grade quick = j.review.grade;
    ++table[static_cast<std::size_t>(deep)][static_cast<std::size_t>(quick)];
    if (deep == quick) ++agree;
    const double quick_loss = j.review.grade == cae::Grade::kBest ? 0 : j.review.loss;
    abs_diff += std::abs(quick_loss - j.golden_loss);
    diff += quick_loss - j.golden_loss;
    if (j.review.grade != cae::Grade::kBest) {
      paired += j.review.error;
      unpaired += j.review.error_unpaired;
      ++graded;
    }
  }
  const auto n = static_cast<double>(jobs.size());
  std::printf("%zu golden positions\n", jobs.size());
  std::printf("loss of the review - deep loss: mean %+.2f pp, mean absolute %.2f pp\n", 100 * diff / n,
              100 * abs_diff / n);
  std::printf("error of a loss (not best, %d): paired %.2f pp, unpaired %.2f pp\n", graded,
              100 * paired / std::max(1, graded), 100 * unpaired / std::max(1, graded));
  std::printf("same grade: %d (%.0f%%)\n\n", agree, 100.0 * agree / n);
  std::printf("%-12s", "deep\\review");
  for (int q = 0; q < cae::kGradeCount; ++q) std::printf("%11s", std::string(cae::grade_name(static_cast<cae::Grade>(q))).c_str());
  std::printf("\n");
  for (int d = 0; d < cae::kGradeCount; ++d) {
    std::printf("%-12s", std::string(cae::grade_name(static_cast<cae::Grade>(d))).c_str());
    for (int q = 0; q < cae::kGradeCount; ++q) std::printf("%11d", table[at(d)][at(q)]);
    std::printf("\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  int seat = -1;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  int limit = 0;
  std::string out_path;
  std::string golden_path;
  bool plant = false;
  std::string elites_path = "data/elites.json";
  cae::ReviewConfig config;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--seat") {
      seat = std::atoi(value().c_str());
    } else if (flag == "--threads") {
      threads = std::atoi(value().c_str());
    } else if (flag == "--first") {
      config.label.first_playouts = std::atoi(value().c_str());
    } else if (flag == "--finalists") {
      config.label.finalists = std::atoi(value().c_str());
    } else if (flag == "--confirm") {
      config.label.confirm_playouts = std::atoi(value().c_str());
    } else if (flag == "--seed") {
      config.label.seed = std::strtoull(value().c_str(), nullptr, 10);
    } else if (flag == "--elites") {
      elites_path = value();
    } else if (flag == "--setup") {
      config.setup = true;
    } else if (flag == "--out") {
      out_path = value();
    } else if (flag == "--golden") {
      golden_path = value();
    } else if (flag == "--plant") {
      plant = true;
    } else if (flag == "--limit") {
      limit = std::atoi(value().c_str());
    } else if (!flag.empty() && flag[0] == '-') {
      usage(argv[0]);
    } else {
      paths.push_back(flag);
    }
  }
  if ((paths.empty() == golden_path.empty()) || threads < 1) usage(argv[0]);

  try {
    const std::vector<cae::ZooBot> pool = cae::load_elites(elites_path);
    std::vector<Job> jobs;
    if (!golden_path.empty()) {
      for (const std::string& line : read_lines(golden_path)) {
        const json g = json::parse(line);
        if (g.at("played").get<int>() < 0) continue;
        if (!config.setup && g.at("phase") == "setup") continue;
        Job job = golden_job(g, plant);
        if (plant && job.golden_best) continue;  // a single move, or the played one is already the worst
        jobs.push_back(std::move(job));
        if (limit > 0 && static_cast<int>(jobs.size()) >= limit) break;
      }
    }
    for (const std::string& path : paths) {
      const json record = cae::read_record_file(path);
      const std::string name = std::filesystem::path(path).stem().string();
      for (cae::ReviewDecision& d : cae::review_decisions(record, seat, config.setup)) {
        Job job;
        job.game = name;
        const std::string player = record.at("players").at(at(d.seat)).value("name", "?");
        job.level = player.substr(0, player.find(' '));
        job.decision = std::move(d);
        jobs.push_back(std::move(job));
      }
    }

    std::fprintf(stderr, "%zu decisions, %d threads\n", jobs.size(), threads);
    std::atomic<int> next{0};
    std::atomic<int> finished{0};
    std::atomic<long long> playouts{0};
    std::mutex print;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (int w = 0; w < threads; ++w) {
      workers.emplace_back([&] {
        for (int i = next++; i < static_cast<int>(jobs.size()); i = next++) {
          Job& job = jobs[at(i)];
          job.review = cae::review_decision(job.decision, config, pool);
          playouts += job.review.playouts;
          const int n = ++finished;
          if (n % 200 == 0) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const std::lock_guard<std::mutex> guard(print);
            std::fprintf(stderr, "%d/%zu decisions, %.0f s\n", n, jobs.size(), s);
          }
        }
      });
    }
    for (std::thread& t : workers) t.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (!out_path.empty()) {
      std::ofstream out(out_path);
      if (!out) throw std::runtime_error("cannot write " + out_path);
      for (const Job& j : jobs) {
        json line = cae::review_json(j.review);
        line["game"] = j.game;
        line["level"] = j.level;
        if (!golden_path.empty()) {
          line["golden_loss"] = j.golden_loss;
          line["golden_best"] = j.golden_best;
          line["golden_error"] = j.golden_error;
        }
        out << line.dump() << "\n";
      }
    }
    if (golden_path.empty()) {
      print_summary(jobs, config.thresholds);
    } else {
      print_golden(jobs, config.thresholds);
    }
    const double decisions = std::max(1.0, static_cast<double>(jobs.size()));
    std::printf("\n%zu decisions, %lld playouts in %.0f s (%.0f per decision, %.2f s per decision on %d threads)\n",
                jobs.size(), static_cast<long long>(playouts), seconds, static_cast<double>(playouts) / decisions,
                seconds / decisions, threads);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
