// catan_map_elites: MAP-Elites over the zoo (include/catan/map_elites.hpp
// explains the method; docs/map-elites.md has the results).
//
//   build/release/tools/catan_map_elites --threads 16                 the search, writes data/elites.json
//   build/release/tools/catan_map_elites --recheck 0 --reeval 0 --out /tmp/plain.json --threads 16
//   build/release/tools/catan_map_elites --describe 4000 --seed 1001 --out data/zoo-styles.json --threads 16
//       styles and scores of the hand-made zoo on the final deals of a search with seed 1, no search
//
// The search:
//   1. seed bots: the 16 hand-made zoo bots (4 archetypes x 4 personalities,
//      unshaken) and --seed-bots random shaken ones;
//   2. every generation, --batch candidates, each a mutated copy of the
//      elite of a random filled cell;
//   3. each candidate plays --games search deals (the same deals for every
//      candidate); its style gives its cell;
//   4. if it beats the cell's elite on those (or the cell is empty), it plays
//      --recheck fresh deals of its own and takes the cell only if it beats
//      the elite's score there too;
//   5. every elite plays --reeval more fresh deals and adds them to its score;
//   6. after the last generation, every elite plays --final deals that no
//      choice was made on: its honest score.
// --recheck 0 --reeval 0 is plain MAP-Elites: whoever looked best on the
// search deals keeps the cell, with that score.
//
// Opponents are random zoo bots (catan_arena's `zoo`). Results do not depend
// on --threads: games are summed in a fixed order and candidates go into the
// archive in the order they were made.

#include "catan/elites.hpp"
#include "catan/map_elites.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using nlohmann::json;
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

struct Options {
  int generations = 60;
  int batch = 128;
  int games = 400;
  int recheck = 800;
  int reeval = 100;
  int final_games = 4000;
  int seed_bots = 64;
  int threads = 1;
  std::uint64_t seed = 1;
  std::uint64_t final_seed = 0;  // 0: seed + 1000
  int describe = -1;             // >= 0: only describe the zoo's styles
  std::string out = "data/elites.json";
};

// One game: deal `index` of `seed` for bot `bot`, summed into `slot`.
struct Job {
  int bot = 0;
  std::uint64_t seed = 0;
  int index = 0;
};

// Plays all jobs on the threads, then sums each bot's games in job order.
std::vector<cae::Evaluation> run(const std::vector<cae::ZooBot>& bots, const std::vector<Job>& jobs, int threads) {
  std::vector<cae::Evaluation> per_job(jobs.size());
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&] {
      for (std::size_t i = next++; i < jobs.size(); i = next++) {
        per_job[i] = cae::evaluate_deal(bots[at(jobs[i].bot)], jobs[i].seed, jobs[i].index);
      }
    });
  }
  for (auto& th : pool) th.join();
  std::vector<cae::Evaluation> out(bots.size());
  for (std::size_t i = 0; i < jobs.size(); ++i) out[at(jobs[i].bot)] += per_job[i];
  return out;
}

// Bot i plays deals ranges[i].second .. + count - 1 of seed ranges[i].first.
std::vector<cae::Evaluation> evaluate(const std::vector<cae::ZooBot>& bots,
                                      const std::vector<std::pair<std::uint64_t, int>>& ranges, int count,
                                      int threads) {
  std::vector<Job> jobs;
  jobs.reserve(bots.size() * at(count));
  for (int b = 0; b < static_cast<int>(bots.size()); ++b) {
    for (int d = 0; d < count; ++d) jobs.push_back({b, ranges[at(b)].first, ranges[at(b)].second + d});
  }
  return run(bots, jobs, threads);
}

// Every bot on the same deals 0 .. count - 1 of `seed`.
std::vector<cae::Evaluation> evaluate(const std::vector<cae::ZooBot>& bots, std::uint64_t seed, int count,
                                      int threads) {
  return evaluate(bots, std::vector<std::pair<std::uint64_t, int>>(bots.size(), {seed, 0}), count, threads);
}

std::string cell_name(int cell) {
  const cae::CellBins b = cae::bins_of_cell(cell);
  return "c" + std::to_string(b.city) + " t" + std::to_string(b.trades) + " d" + std::to_string(b.dev_cards);
}

std::string bot_name(const cae::ZooBot& b) {
  return std::string(cae::archetype_name(b.archetype)) + "/" + std::string(cae::personality_name(b.personality));
}

std::vector<cae::ZooBot> seed_population(const Options& o) {
  std::vector<cae::ZooBot> out;
  for (int a = 0; a < cae::kArchetypeCount; ++a) {
    for (int p = 0; p < cae::kPersonalityCount; ++p) {
      out.push_back(cae::make_zoo_bot(static_cast<cae::Archetype>(a), static_cast<cae::Personality>(p), 0));
    }
  }
  cae::Rng rng(o.seed * 7919 + 17);
  for (int i = 0; i < o.seed_bots; ++i) out.push_back(cae::random_zoo_bot(rng));
  return out;
}

// --describe: where the hand-made zoo falls on each axis, and how strong
// each of it is. With --out it also writes them as JSON. With --seed 1001
// they play the same deals as the final scores of a search with seed 1.
void describe(const Options& o, bool write) {
  std::vector<cae::ZooBot> bots;
  for (int a = 0; a < cae::kArchetypeCount; ++a) {
    for (int p = 0; p < cae::kPersonalityCount; ++p) {
      bots.push_back(cae::make_zoo_bot(static_cast<cae::Archetype>(a), static_cast<cae::Personality>(p), 0));
    }
  }
  const auto evals = evaluate(bots, o.seed, o.describe, o.threads);
  std::printf("%-22s %6s %6s %7s %7s  %s\n", "bot", "wins", "+-", "cities", "trades", "dev cards");
  json zoo = json::array();
  for (std::size_t i = 0; i < bots.size(); ++i) {
    const cae::Behavior b = evals[i].behavior();
    const int cell = cae::cell_of(cae::bins_of(b));
    std::printf("%-22s %5.1f%% %5.1f %7.2f %7.2f %7.2f   -> %s\n", bot_name(bots[i]).c_str(),
                100 * evals[i].win_rate(), 100 * evals[i].margin(), b.city_share, b.trades, b.dev_cards,
                cell_name(cell).c_str());
    zoo.push_back({{"name", bot_name(bots[i])}, {"cell", cell}, {"eval", cae::evaluation_json(evals[i])}});
  }
  if (write) {
    std::ofstream out(o.out);
    if (!out) throw std::runtime_error("cannot write " + o.out);
    out << json{{"seed", o.seed}, {"games", o.describe}, {"zoo", zoo}}.dump(1) << "\n";
    std::printf("wrote %s\n", o.out.c_str());
  }
}

struct Candidate {
  cae::ZooBot bot;
  int parent = -1;
  int born = 0;
};

struct Counters {
  long candidates = 0;
  long passed_search = 0;  // beat the elite on the search deals
  long inserted = 0;       // took the cell
  long new_cells = 0;
  long games = 0;
};

// Step 5: every elite plays its next --reeval fresh deals.
void reevaluate(cae::Archive& archive, const Options& o, Counters& c) {
  if (o.reeval <= 0) return;
  const std::vector<int> cells = archive.filled_cells();
  std::vector<cae::ZooBot> bots;
  std::vector<std::pair<std::uint64_t, int>> ranges;
  for (int cell : cells) {
    const cae::Elite& e = *archive.cells[at(cell)];
    bots.push_back(e.bot);
    ranges.emplace_back(cae::fresh_seed(o.seed, e.born), static_cast<int>(e.fresh.games));
  }
  const auto r = evaluate(bots, ranges, o.reeval, o.threads);
  for (std::size_t k = 0; k < cells.size(); ++k) archive.cells[at(cells[k])]->fresh += r[k];
  c.games += static_cast<long>(cells.size()) * o.reeval;
}

// Steps 3 and 4 for one batch of candidates.
void offer(cae::Archive& archive, const std::vector<Candidate>& batch, const Options& o, Counters& c) {
  std::vector<cae::ZooBot> bots;
  for (const auto& k : batch) bots.push_back(k.bot);
  const auto search = evaluate(bots, o.seed, o.games, o.threads);
  c.candidates += static_cast<long>(batch.size());
  c.games += static_cast<long>(batch.size()) * o.games;

  // Who beats the elite of its cell as the archive stood before the batch
  std::vector<int> passing;
  for (int i = 0; i < static_cast<int>(batch.size()); ++i) {
    const int cell = cae::cell_of(cae::bins_of(search[at(i)].behavior()));
    const auto& elite = archive.cells[at(cell)];
    if (!elite || search[at(i)].win_rate() > elite->score().win_rate()) passing.push_back(i);
  }
  c.passed_search += static_cast<long>(passing.size());

  std::vector<cae::Evaluation> recheck(batch.size());
  if (o.recheck > 0 && !passing.empty()) {
    std::vector<cae::ZooBot> again;
    std::vector<std::pair<std::uint64_t, int>> ranges;
    for (int i : passing) {
      again.push_back(batch[at(i)].bot);
      ranges.emplace_back(cae::fresh_seed(o.seed, batch[at(i)].born), 0);
    }
    const auto r = evaluate(again, ranges, o.recheck, o.threads);
    for (std::size_t k = 0; k < passing.size(); ++k) recheck[at(passing[k])] = r[k];
    c.games += static_cast<long>(passing.size()) * o.recheck;
  }

  for (int i : passing) {
    const int cell = cae::cell_of(cae::bins_of(search[at(i)].behavior()));
    cae::Elite e{batch[at(i)].bot, search[at(i)], recheck[at(i)], {}, batch[at(i)].born, batch[at(i)].parent};
    auto& slot = archive.cells[at(cell)];
    if (!slot) ++c.new_cells;
    if (!slot || e.score().win_rate() > slot->score().win_rate()) {
      slot = e;
      ++c.inserted;
    }
  }
}

json archive_json(const cae::Archive& archive, const Options& o, const Counters& c, double seconds) {
  std::vector<int> cells = archive.filled_cells();
  std::stable_sort(cells.begin(), cells.end(), [&](int a, int b) {
    return archive.cells[at(a)]->final.win_rate() > archive.cells[at(b)]->final.win_rate();
  });
  json elites = json::array();
  for (int cell : cells) {
    const cae::Elite& e = *archive.cells[at(cell)];
    const cae::CellBins b = cae::bins_of_cell(cell);
    elites.push_back({{"cell", cell},
                      {"bins", {b.city, b.trades, b.dev_cards}},
                      {"born", e.born},
                      {"parent", e.parent},
                      {"search", cae::evaluation_json(e.search)},
                      {"fresh", cae::evaluation_json(e.fresh)},
                      {"final", cae::evaluation_json(e.final)},
                      {"bot", cae::zoo_bot_json(e.bot)}});
  }
  return {{"version", 1},
          {"grid",
           {{"city_share", cae::kCityShareEdges}, {"trades", cae::kTradeEdges}, {"dev_cards", cae::kDevCardEdges}}},
          {"run",
           {{"generations", o.generations},
            {"batch", o.batch},
            {"games", o.games},
            {"recheck", o.recheck},
            {"reeval", o.reeval},
            {"final", o.final_games},
            {"seed_bots", o.seed_bots},
            {"seed", o.seed},
            {"final_seed", o.final_seed},
            {"candidates", c.candidates},
            {"passed_search", c.passed_search},
            {"inserted", c.inserted},
            {"games_played", c.games},
            {"seconds", seconds}}},
          {"elites", elites}};
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--generations N] [--batch N] [--games N] [--recheck N] [--reeval N] [--final N]\n"
               "          [--seed-bots N] [--threads T] [--seed S] [--final-seed S] [--out PATH] [--describe N]\n",
               argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  bool out_given = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (a == "--generations") o.generations = std::stoi(value());
    else if (a == "--batch") o.batch = std::stoi(value());
    else if (a == "--games") o.games = std::stoi(value());
    else if (a == "--recheck") o.recheck = std::stoi(value());
    else if (a == "--reeval") o.reeval = std::stoi(value());
    else if (a == "--final") o.final_games = std::stoi(value());
    else if (a == "--seed-bots") o.seed_bots = std::stoi(value());
    else if (a == "--threads") o.threads = std::max(1, std::stoi(value()));
    else if (a == "--seed") o.seed = std::stoull(value());
    else if (a == "--final-seed") o.final_seed = std::stoull(value());
    else if (a == "--out") {
      o.out = value();
      out_given = true;
    }
    else if (a == "--describe") o.describe = std::stoi(value());
    else usage(argv[0]);
  }
  if (o.final_seed == 0) o.final_seed = o.seed + 1000;
  if (o.games < 1 || o.batch < 1 || o.generations < 0 || o.recheck < 0 || o.reeval < 0 || o.final_games < 0) {
    usage(argv[0]);
  }

  try {
    if (o.describe >= 0) {
      describe(o, out_given);
      return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    cae::Archive archive;
    Counters c;
    int born = 0;

    std::vector<Candidate> seeds;
    for (const auto& b : seed_population(o)) seeds.push_back({b, -1, born++});
    offer(archive, seeds, o, c);
    std::printf("seed bots: %zu, %d cells filled, mean score %.1f%%\n", seeds.size(), archive.filled(),
                100 * archive.mean_score());

    cae::Rng rng(o.seed * 104729 + 3);
    for (int g = 1; g <= o.generations; ++g) {
      const std::vector<int> cells = archive.filled_cells();
      std::vector<Candidate> batch;
      for (int k = 0; k < o.batch; ++k) {
        const int parent = cells[at(rng.below(static_cast<int>(cells.size())))];
        batch.push_back({cae::mutate(archive.cells[at(parent)]->bot, rng), parent, born++});
      }
      const long inserted = c.inserted;
      offer(archive, batch, o, c);
      reevaluate(archive, o, c);
      if (g % 5 == 0 || g == o.generations) {
        std::printf("generation %3d: %2d / %d cells, mean score %.1f%%, %ld taken this generation, %.0f s\n", g,
                    archive.filled(), cae::kStyleCells, 100 * archive.mean_score(), c.inserted - inserted, elapsed());
        std::fflush(stdout);
      }
    }

    // Step 5: honest scores
    const std::vector<int> cells = archive.filled_cells();
    if (o.final_games > 0) {
      std::vector<cae::ZooBot> bots;
      for (int cell : cells) bots.push_back(archive.cells[at(cell)]->bot);
      const auto finals = evaluate(bots, o.final_seed, o.final_games, o.threads);
      for (std::size_t k = 0; k < cells.size(); ++k) archive.cells[at(cells[k])]->final = finals[k];
      c.games += static_cast<long>(cells.size()) * o.final_games;
    }

    std::printf("\n%-10s %-22s %8s %8s %8s %6s %6s %5s\n", "cell", "from", "search", "fresh", "final", "trades",
                "devs", "rob");
    double search_gap = 0;
    double fresh_gap = 0;
    long fresh_games = 0;
    for (int cell : cells) {
      const cae::Elite& e = *archive.cells[at(cell)];
      const cae::Behavior b = e.final.behavior();
      std::printf("%-10s %-22s %7.1f%% %7.1f%% %7.1f%% %6.2f %6.2f %5.2f\n", cell_name(cell).c_str(),
                  bot_name(e.bot).c_str(), 100 * e.search.win_rate(), 100 * e.fresh.win_rate(),
                  100 * e.final.win_rate(), b.trades, b.dev_cards, e.bot.weights.leader_focus);
      search_gap += e.search.win_rate() - e.final.win_rate();
      fresh_gap += e.fresh.win_rate() - e.final.win_rate();
      fresh_games += e.fresh.games;
    }
    const auto n = static_cast<double>(cells.size());
    std::printf("\n%ld candidates, %ld beat their cell's elite on the search deals, %ld took a cell\n", c.candidates,
                c.passed_search, c.inserted);
    if (o.final_games > 0 && n > 0) {
      std::printf("mean score above final: search %+.1f points", 100 * search_gap / n);
      if (fresh_games > 0) {
        std::printf(", fresh %+.1f points (%.0f fresh games per elite)", 100 * fresh_gap / n,
                    static_cast<double>(fresh_games) / n);
      }
      std::printf("\n");
    }
    std::printf("%ld games in %.0f s (%.0f games/s)\n", c.games, elapsed(), static_cast<double>(c.games) / elapsed());

    std::ofstream out(o.out);
    if (!out) throw std::runtime_error("cannot write " + o.out);
    out << archive_json(archive, o, c, elapsed()).dump(1) << "\n";
    std::printf("wrote %s\n", o.out.c_str());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "catan_map_elites: %s\n", e.what());
    return 1;
  }
  return 0;
}
