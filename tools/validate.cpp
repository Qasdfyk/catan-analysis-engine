// catan_validate: the frozen validation set A and the baseline model.
//
// Set A (docs/arena.md) is a fixed set of positions from native zoo
// self-play with known outcomes. Anything that estimates chances to win, the
// Monte Carlo of today or a value network later, is scored on it with the
// same calibration table and Brier score. The set never goes into training.
//
//   make      plays zoo games and writes positions, one game per line (JSON)
//               catan_validate make --games 1000 --seed 1001 --positions 4 | gzip > data/set-a.jsonl.gz
//   train     fits the baseline model on other games (other seeds) and writes it
//               catan_validate train --games 4000 --seed 2001 --positions 4 > data/baseline.json
//   eval      scores an estimator on a set read from stdin
//               gzip -dc data/set-a.jsonl.gz | catan_validate eval --model data/baseline.json
//               gzip -dc data/set-a.jsonl.gz | catan_validate eval --playouts 200 --threads 16 --bots record
//   sanity    the sanity positions C (below) for one bot
//               catan_validate sanity --bot hard --games 200 --seed 3001
//
// Positions are the points just before a roll, after the setup, drawn at
// random from each game: early, middle and late game all show up. Each line
// keeps the board, the positions as snapshots (record.hpp), the bots that
// played and the winner, so the set does not depend on the engine version
// that made it: it is read back with game_from_snapshot().
//
// The baseline model: a conditional logit (multinomial logistic regression
// over the seats of one position). Each seat gets a score w . x from a few
// features of what is on the table; its chance is exp(score) over the sum of
// exp(score) of all seats, so the chances add up to 1. Fitted by Newton's
// method on the log-likelihood of the winners. A value network has to beat it
// clearly on set A, or it learned nothing a few numbers do not already tell.

#include "catan/calibration.hpp"
#include "catan/random_player.hpp"
#include "catan/record.hpp"
#include "catan/winrate.hpp"
#include "catan/zoo.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// --- Games -------------------------------------------------------------------------

struct PlayedGame {
  cae::Board board;
  std::array<cae::ZooBot, cae::kMaxPlayers> bots{};
  std::vector<cae::Game> positions;
  int winner = -1;
};

// Zoo game i of a seed, as catan_selfplay --bots zoo plays it, with `count`
// positions drawn from the points just before a roll.
PlayedGame play_game(std::uint64_t seed, int i, int count) {
  cae::Rng rng((seed << 32) ^ static_cast<std::uint64_t>(i));
  cae::ZooTable table;
  table.game = cae::random_game(rng);
  for (auto& bot : table.bots) bot = cae::random_zoo_bot(rng);
  PlayedGame out;
  out.board = table.game.board;
  out.bots = table.bots;
  std::vector<cae::Game> candidates;
  int last_turn = -1;
  for (int step = 0; step < 40'000 && !table.game.game_over; ++step) {
    const cae::Game& g = table.game;
    if (g.phase == cae::Phase::kRollDice && g.turn_number != last_turn) {
      candidates.push_back(g);
      last_turn = g.turn_number;
    }
    (void)cae::play_zoo_step(table, rng);
  }
  out.winner = table.game.winner;
  cae::Rng pick(seed * 7919 + static_cast<std::uint64_t>(i));
  for (int k = static_cast<int>(candidates.size()) - 1; k > 0; --k) {
    std::swap(candidates[at(k)], candidates[at(pick.below(k + 1))]);
  }
  candidates.resize(std::min(candidates.size(), at(count)));
  std::sort(candidates.begin(), candidates.end(),
            [](const cae::Game& a, const cae::Game& b) { return a.turn_number < b.turn_number; });
  out.positions = std::move(candidates);
  return out;
}

json bot_json(const cae::ZooBot& b) {
  return {{"archetype", cae::archetype_name(b.archetype)},
          {"personality", cae::personality_name(b.personality)},
          {"seed", b.seed}};
}

cae::ZooBot bot_from_json(const json& j) {
  int a = 0;
  int p = 0;
  for (int i = 0; i < cae::kArchetypeCount; ++i) {
    if (j.at("archetype") == cae::archetype_name(static_cast<cae::Archetype>(i))) a = i;
  }
  for (int i = 0; i < cae::kPersonalityCount; ++i) {
    if (j.at("personality") == cae::personality_name(static_cast<cae::Personality>(i))) p = i;
  }
  return cae::make_zoo_bot(static_cast<cae::Archetype>(a), static_cast<cae::Personality>(p),
                           j.at("seed").get<std::uint64_t>());
}

std::vector<PlayedGame> play_games(std::uint64_t seed, int games, int positions, int threads) {
  std::vector<PlayedGame> out(at(games));
  std::atomic<int> next{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    pool.emplace_back([&] {
      for (int i = next++; i < games; i = next++) out[at(i)] = play_game(seed, i, positions);
    });
  }
  for (auto& th : pool) th.join();
  std::erase_if(out, [](const PlayedGame& g) { return g.winner < 0; });
  return out;
}

// --- The baseline model -----------------------------------------------------------

constexpr int kFeatures = 8;
const std::array<const char*, kFeatures> kFeatureNames = {
    "points",       // visible and hidden victory points
    "production",   // pips of all its buildings, cities twice, robbed hex left out
    "cards",        // resource cards in hand
    "dev_cards",    // development cards held, new ones included
    "knights",      // knights played
    "road_length",  // longest road
    "to_move",      // 1 for the player about to roll
    "ports",        // ports it can use
};

std::array<double, kFeatures> features(const cae::Game& g, int seat) {
  const cae::Player& p = g.players[at(seat)];
  double production = 0;
  for (int h = 0; h < cae::kHexCount; ++h) {
    const int n = g.board.number[at(h)];
    if (n == 0 || h == g.robber_hex) continue;
    const int pips = n > 7 ? 13 - n : n - 1;
    const cae::NodeMask around = cae::kTopology.hex_node_mask[at(h)];
    production += pips * (cae::count(p.settlements & around) + 2 * cae::count(p.cities & around));
  }
  int dev = 0;
  for (int c = 0; c < cae::kDevCardKinds; ++c) dev += p.dev_cards[at(c)] + p.new_dev_cards[at(c)];
  int ports = 0;
  for (int i = 0; i < g.board.port_count; ++i) ports += (g.board.ports[at(i)].nodes & p.buildings()) != 0 ? 1 : 0;
  return {static_cast<double>(g.visible_points(seat) + g.hidden_points(seat)),
          production,
          static_cast<double>(p.card_count()),
          static_cast<double>(dev),
          static_cast<double>(p.knights),
          static_cast<double>(g.road_lengths[at(seat)]),
          seat == g.current ? 1.0 : 0.0,
          static_cast<double>(ports)};
}

using Weights = std::array<double, kFeatures>;

std::array<double, cae::kMaxPlayers> chances(const Weights& w, const cae::Game& g) {
  std::array<double, cae::kMaxPlayers> score{};
  double top = -1e300;
  for (int s = 0; s < g.player_count; ++s) {
    const auto x = features(g, s);
    for (int k = 0; k < kFeatures; ++k) score[at(s)] += w[at(k)] * x[at(k)];
    top = std::max(top, score[at(s)]);
  }
  double sum = 0;
  for (int s = 0; s < g.player_count; ++s) sum += score[at(s)] = std::exp(score[at(s)] - top);
  for (int s = 0; s < g.player_count; ++s) score[at(s)] /= sum;
  return score;
}

// Newton's method on the log-likelihood of the winners, with a small ridge
// so a feature that never varies cannot blow up. Gradient: the winner's
// features minus their expected value; Hessian: minus their covariance.
Weights fit(const std::vector<PlayedGame>& games) {
  Weights w{};
  constexpr double kRidge = 1e-3;
  for (int iteration = 0; iteration < 25; ++iteration) {
    std::array<double, kFeatures> grad{};
    std::array<std::array<double, kFeatures>, kFeatures> hess{};
    double loglik = 0;
    for (const auto& game : games) {
      for (const auto& g : game.positions) {
        const auto p = chances(w, g);
        std::array<double, kFeatures> mean{};
        std::array<std::array<double, kFeatures>, cae::kMaxPlayers> x{};
        for (int s = 0; s < g.player_count; ++s) {
          x[at(s)] = features(g, s);
          for (int k = 0; k < kFeatures; ++k) mean[at(k)] += p[at(s)] * x[at(s)][at(k)];
        }
        loglik += std::log(std::max(1e-300, p[at(game.winner)]));
        for (int k = 0; k < kFeatures; ++k) grad[at(k)] += x[at(game.winner)][at(k)] - mean[at(k)];
        for (int s = 0; s < g.player_count; ++s) {
          for (int a = 0; a < kFeatures; ++a) {
            for (int b = 0; b < kFeatures; ++b) {
              hess[at(a)][at(b)] += p[at(s)] * (x[at(s)][at(a)] - mean[at(a)]) * (x[at(s)][at(b)] - mean[at(b)]);
            }
          }
        }
      }
    }
    // Solve (H + ridge) step = grad - ridge w by Gaussian elimination.
    std::array<std::array<double, kFeatures + 1>, kFeatures> m{};
    for (int a = 0; a < kFeatures; ++a) {
      for (int b = 0; b < kFeatures; ++b) m[at(a)][at(b)] = hess[at(a)][at(b)] + (a == b ? kRidge : 0.0);
      m[at(a)][kFeatures] = grad[at(a)] - kRidge * w[at(a)];
    }
    for (int c = 0; c < kFeatures; ++c) {
      int pivot = c;
      for (int r = c + 1; r < kFeatures; ++r) {
        if (std::abs(m[at(r)][at(c)]) > std::abs(m[at(pivot)][at(c)])) pivot = r;
      }
      std::swap(m[at(c)], m[at(pivot)]);
      for (int r = 0; r < kFeatures; ++r) {
        if (r == c || m[at(c)][at(c)] == 0) continue;
        const double f = m[at(r)][at(c)] / m[at(c)][at(c)];
        for (int k = c; k <= kFeatures; ++k) m[at(r)][at(k)] -= f * m[at(c)][at(k)];
      }
    }
    double change = 0;
    for (int k = 0; k < kFeatures; ++k) {
      const double step = m[at(k)][at(k)] != 0 ? m[at(k)][kFeatures] / m[at(k)][at(k)] : 0.0;
      w[at(k)] += step;
      change = std::max(change, std::abs(step));
    }
    std::fprintf(stderr, "iteration %2d: log-likelihood %.2f, largest step %.2e\n", iteration, loglik, change);
    if (change < 1e-9) break;
  }
  return w;
}

// --- Reading a set ---------------------------------------------------------------

struct SetGame {
  std::vector<cae::Game> positions;
  cae::SeatBots bots;
  int winner = -1;
};

std::vector<SetGame> read_set(std::istream& in) {
  std::vector<SetGame> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const json j = json::parse(line);
    SetGame g;
    g.winner = j.at("winner").get<int>();
    for (const auto& p : j.at("positions")) g.positions.push_back(cae::game_from_snapshot(j.at("board"), p));
    const auto& bots = j.at("bots");
    for (std::size_t s = 0; s < bots.size(); ++s) g.bots[s] = bot_from_json(bots[s]);
    out.push_back(std::move(g));
  }
  return out;
}

struct Options {
  int games = 1000;
  std::uint64_t seed = 1001;
  int positions = 4;
  int threads = 1;
  int playouts = 200;
  bool record_bots = false;
  std::string model;
  int bootstrap = 1000;
  std::string bot = "hard";
};

// --- Sanity positions C ------------------------------------------------------------
//
// Positions with an obvious right answer, like EPD tests in chess. Each
// scenario is set up on many real positions from zoo games (same idea, other
// boards and hands), and checks what the bot does, not one exact move:
//   win-settlement   9 points and a settlement's cards: win this turn
//   win-city         9 points and a city's cards: win this turn
//   win-army         8 points, one knight from Largest Army: win this turn
//   refuse-leader    a player one step from winning offers a trade: say no
//   robber-leader    the robber goes next to the leader, not next to itself
//   steal-leader     of the players it can rob, it robs the leader
//   first-spot       the first settlement goes on one of the 5 richest free corners
// "Points" are visible ones plus victory point cards (added to reach the
// number); the leader gets Longest Road and Largest Army to reach 8 visible.

struct SanityResult {
  int passed = 0;
  int tried = 0;
};

constexpr std::array<const char*, 7> kScenarios = {"win-settlement", "win-city",     "win-army",  "refuse-leader",
                                                   "robber-leader",  "steal-leader", "first-spot"};

int total_points(const cae::Game& g, int s) { return g.visible_points(s) + g.hidden_points(s); }

void add_vp_cards(cae::Game& g, int seat, int target) {
  const int missing = target - total_points(g, seat);
  g.players[at(seat)].dev_cards[at(static_cast<int>(cae::DevCard::kVictoryPoint))] += static_cast<std::uint8_t>(missing);
}

void to_main_phase(cae::Game& g, int seat) {
  g.current = seat;
  g.phase = cae::Phase::kMain;
  g.dice_roll = 8;
  g.dev_card_played = false;
  g.offer = {};
  for (auto& d : g.pending_discards) d = -1;
}

// Makes `leader` one step from winning in what everyone sees: both awards.
bool make_leader(cae::Game& g, int leader) {
  for (int s = 0; s < g.player_count; ++s) {
    g.players[at(s)].knights = std::min(g.players[at(s)].knights, 2);
    g.road_lengths[at(s)] = std::min(g.road_lengths[at(s)], 4);
  }
  g.largest_army = leader;
  g.players[at(leader)].knights = 3;
  g.longest_road = leader;
  g.road_lengths[at(leader)] = 5;
  for (int s = 0; s < g.player_count; ++s) {
    if (s != leader && g.visible_points(s) >= g.visible_points(leader)) return false;  // a clear leader only
  }
  return g.visible_points(leader) >= 8 && total_points(g, leader) < g.settings.victory_points;
}

// Plays the seat's turn with the bot (others answer as hard bots) until the
// turn passes or the game ends.
bool wins_this_turn(cae::Game g, int seat, const cae::ZooBot& bot) {
  cae::Rng rng(7);
  cae::ZooTable t;
  t.game = g;
  for (auto& b : t.bots) b = cae::hard_bot();
  t.bots[at(seat)] = bot;
  const int turn = g.turn_number;
  for (int i = 0; i < 60 && !t.game.game_over && t.game.turn_number == turn; ++i) (void)cae::play_zoo_step(t, rng);
  return t.game.game_over && t.game.winner == seat;
}

int pips_at(const cae::Game& g, int node) {
  int sum = 0;
  for (int h = 0; h < cae::kHexCount; ++h) {
    const int n = g.board.number[at(h)];
    if (n != 0 && cae::contains(cae::kTopology.hex_node_mask[at(h)], node)) sum += n > 7 ? 13 - n : n - 1;
  }
  return sum;
}

std::optional<bool> run_scenario(int scenario, const cae::Game& base, int seat, const cae::ZooBot& bot) {
  cae::Game g = base;
  const cae::TableMemory memory;
  const int leader = (seat + 1) % g.player_count;
  switch (scenario) {
    case 0:
    case 1: {  // win with a settlement / a city
      to_main_phase(g, seat);
      g.players[at(seat)].resources = scenario == 0 ? cae::kSettlementCost : cae::kCityCost;
      if (scenario == 0 && g.legal_settlements(seat) == 0) return std::nullopt;
      if (scenario == 1 && g.legal_cities(seat) == 0) return std::nullopt;
      if (total_points(g, seat) > 9) return std::nullopt;
      add_vp_cards(g, seat, 9);
      return wins_this_turn(g, seat, bot);
    }
    case 2: {  // win by taking Largest Army
      to_main_phase(g, seat);
      const int holder = leader;
      for (int s = 0; s < g.player_count; ++s) g.players[at(s)].knights = std::min(g.players[at(s)].knights, 2);
      g.largest_army = holder;
      g.players[at(holder)].knights = 3;
      g.players[at(seat)].knights = 3;
      g.players[at(seat)].dev_cards[at(static_cast<int>(cae::DevCard::kKnight))] = 1;
      g.players[at(seat)].resources = {};
      if (total_points(g, seat) > 8) return std::nullopt;
      add_vp_cards(g, seat, 8);
      if (total_points(g, holder) >= g.settings.victory_points) return std::nullopt;
      return wins_this_turn(g, seat, bot);
    }
    case 3: {  // refuse the leader's trade
      if (!make_leader(g, leader)) return std::nullopt;
      to_main_phase(g, leader);
      cae::Player& me = g.players[at(seat)];
      me.resources = {1, 1, 1, 1, 0};  // has everything but ore; the offer gives ore
      g.players[at(leader)].resources = {0, 0, 0, 0, 2};
      cae::Cards give{0, 0, 0, 0, 1};
      cae::Cards get{0, 0, 1, 0, 0};
      if (g.offer_trade(leader, give, get) != cae::Error::kNone) return std::nullopt;
      const auto a = cae::zoo_decide(g, seat, bot, memory);
      return a && a->kind == cae::ActionKind::kRespondTrade && !a->accept;
    }
    case 4:
    case 5: {  // the robber and the steal
      if (!make_leader(g, leader)) return std::nullopt;
      to_main_phase(g, seat);
      for (int s = 0; s < g.player_count; ++s) g.players[at(s)].resources = {1, 1, 1, 0, 0};
      g.phase = cae::Phase::kMoveRobber;
      g.return_phase = cae::Phase::kMain;
      const std::uint32_t legal = g.legal_robber_hexes(seat);
      const cae::NodeMask mine = g.players[at(seat)].buildings();
      const cae::NodeMask theirs = g.players[at(leader)].buildings();
      if (scenario == 4) {
        bool exists = false;
        for (int h = 0; h < cae::kHexCount; ++h) {
          const cae::NodeMask around = cae::kTopology.hex_node_mask[at(h)];
          if (((legal >> h) & 1U) != 0 && (around & theirs) != 0 && (around & mine) == 0) exists = true;
        }
        if (!exists) return std::nullopt;
        const auto a = cae::zoo_decide(g, seat, bot, memory);
        if (!a || a->kind != cae::ActionKind::kMoveRobber) return false;
        const cae::NodeMask around = cae::kTopology.hex_node_mask[at(a->target)];
        return (around & theirs) != 0 && (around & mine) == 0;
      }
      // A hex where the leader and someone else can be robbed
      for (int h = 0; h < cae::kHexCount; ++h) {
        if (((legal >> h) & 1U) == 0) continue;
        cae::Game t = g;
        if (t.move_robber(seat, h) != cae::Error::kNone || t.phase != cae::Phase::kSteal) continue;
        const std::uint8_t victims = t.steal_victims(seat);
        if (((victims >> leader) & 1U) == 0 || std::popcount(static_cast<unsigned>(victims)) < 2) continue;
        const auto a = cae::zoo_decide(t, seat, bot, memory);
        return a && a->kind == cae::ActionKind::kSteal && a->target == leader;
      }
      return std::nullopt;
    }
    case 6: {  // the first settlement on a rich corner
      cae::Rng rng(static_cast<std::uint64_t>(seat) * 1000 + static_cast<std::uint64_t>(base.turn_number));
      cae::Game fresh = cae::random_game(rng);
      const int first = fresh.current;
      const auto a = cae::zoo_decide(fresh, first, bot, memory);
      if (!a || a->kind != cae::ActionKind::kBuildSettlement) return false;
      std::vector<int> pips;
      cae::for_each_node(fresh.legal_settlements(first), [&](int n) { pips.push_back(pips_at(fresh, n)); });
      std::sort(pips.rbegin(), pips.rend());
      return pips_at(fresh, a->target) >= pips[std::min<std::size_t>(4, pips.size() - 1)];
    }
    default: return std::nullopt;
  }
}

int sanity(const Options& o, const std::string& bot_name) {
  cae::ZooBot bot = cae::hard_bot();
  if (bot_name != "hard") {
    const auto slash = bot_name.find('/');
    int a = -1;
    int p = -1;
    for (int i = 0; i < cae::kArchetypeCount; ++i) {
      if (bot_name.substr(0, slash) == cae::archetype_name(static_cast<cae::Archetype>(i))) a = i;
    }
    for (int i = 0; i < cae::kPersonalityCount; ++i) {
      if (slash != std::string::npos && bot_name.substr(slash + 1) == cae::personality_name(static_cast<cae::Personality>(i))) p = i;
    }
    if (a < 0 || p < 0) {
      std::fprintf(stderr, "unknown bot %s (hard or ARCHETYPE/PERSONALITY)\n", bot_name.c_str());
      return 2;
    }
    bot = cae::make_zoo_bot(static_cast<cae::Archetype>(a), static_cast<cae::Personality>(p), 0);
  }
  // Real mid-game positions: one per game, the latest of its drawn points.
  const auto games = play_games(o.seed, o.games, o.positions, o.threads);
  std::array<SanityResult, kScenarios.size()> results{};
  for (const auto& game : games) {
    const cae::Game& base = game.positions.back();
    for (std::size_t k = 0; k < kScenarios.size(); ++k) {
      for (int seat = 0; seat < base.player_count; ++seat) {
        const auto ok = run_scenario(static_cast<int>(k), base, seat, bot);
        if (!ok) continue;
        ++results[k].tried;
        if (*ok) ++results[k].passed;
        break;  // one seat per game and scenario
      }
    }
  }
  std::printf("sanity positions C, bot %s, from %zu games (seed %llu)\n", bot_name.c_str(), games.size(),
              static_cast<unsigned long long>(o.seed));
  int passed = 0;
  int tried = 0;
  for (std::size_t k = 0; k < kScenarios.size(); ++k) {
    std::printf("  %-15s %3d / %3d\n", kScenarios[k], results[k].passed, results[k].tried);
    passed += results[k].passed;
    tried += results[k].tried;
  }
  std::printf("  %-15s %3d / %3d\n", "all", passed, tried);
  return 0;
}

[[noreturn]] void usage() {
  std::fprintf(stderr,
               "usage: catan_validate make  [--games N] [--seed S] [--positions K] [--threads T] > set.jsonl\n"
               "       catan_validate train [--games N] [--seed S] [--positions K] [--threads T] > model.json\n"
               "       catan_validate eval  (--model model.json | --playouts N [--bots record|zoo]) [--threads T]\n"
               "                            < set.jsonl\n"
               "       catan_validate sanity [--bot hard|ARCHETYPE/PERSONALITY] [--games N] [--seed S]\n");
  std::exit(2);
}

int make(const Options& o) {
  const auto games = play_games(o.seed, o.games, o.positions, o.threads);
  for (std::size_t i = 0; i < games.size(); ++i) {
    const PlayedGame& g = games[i];
    json positions = json::array();
    for (const auto& p : g.positions) positions.push_back(cae::snapshot(p));
    json bots = json::array();
    for (int s = 0; s < g.positions.front().player_count; ++s) bots.push_back(bot_json(g.bots[at(s)]));
    std::cout << json{{"seed", o.seed}, {"winner", g.winner}, {"bots", bots}, {"board", cae::board_json(g.board)},
                      {"positions", positions}}
                     .dump()
              << '\n';
  }
  std::fprintf(stderr, "%zu finished games of %d, %d positions each at most\n", games.size(), o.games, o.positions);
  return 0;
}

int train(const Options& o) {
  const auto games = play_games(o.seed, o.games, o.positions, o.threads);
  std::fprintf(stderr, "fitting on %zu games\n", games.size());
  const Weights w = fit(games);
  json weights = json::object();
  for (int k = 0; k < kFeatures; ++k) weights[kFeatureNames[at(k)]] = w[at(k)];
  std::cout << json{{"model", "conditional logit"},
                    {"trained_on", {{"games", games.size()}, {"seed", o.seed}, {"positions_per_game", o.positions}}},
                    {"weights", weights}}
                   .dump(2)
            << '\n';
  return 0;
}

int eval(const Options& o) {
  const auto set = read_set(std::cin);
  if (set.empty()) {
    std::fprintf(stderr, "no games on stdin\n");
    return 1;
  }
  std::vector<std::vector<cae::Prediction>> by_game(set.size());
  const auto t0 = std::chrono::steady_clock::now();
  long positions = 0;
  if (!o.model.empty()) {
    const json m = cae::read_record_file(o.model);
    Weights w{};
    for (int k = 0; k < kFeatures; ++k) w[at(k)] = m.at("weights").at(kFeatureNames[at(k)]).get<double>();
    for (std::size_t i = 0; i < set.size(); ++i) {
      for (const auto& g : set[i].positions) {
        const auto p = chances(w, g);
        for (int s = 0; s < g.player_count; ++s) by_game[i].push_back({p[at(s)], s == set[i].winner ? 1 : 0});
        ++positions;
      }
    }
    std::printf("baseline model %s on %zu games\n", o.model.c_str(), set.size());
  } else {
    struct Job {
      int game;
      int position;
    };
    std::vector<Job> jobs;
    for (std::size_t i = 0; i < set.size(); ++i) {
      for (std::size_t k = 0; k < set[i].positions.size(); ++k) jobs.push_back({static_cast<int>(i), static_cast<int>(k)});
    }
    std::vector<cae::WinCounts> counts(jobs.size());
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    const cae::SeatBots none{};
    for (int t = 0; t < o.threads; ++t) {
      pool.emplace_back([&] {
        for (int j = next++; j < static_cast<int>(jobs.size()); j = next++) {
          const SetGame& g = set[at(jobs[at(j)].game)];
          counts[at(j)] = cae::play_out_chances(g.positions[at(jobs[at(j)].position)], 0, o.playouts,
                                                o.seed + static_cast<std::uint64_t>(j), o.record_bots ? g.bots : none);
        }
      });
    }
    for (auto& th : pool) th.join();
    for (std::size_t j = 0; j < jobs.size(); ++j) {
      const SetGame& g = set[at(jobs[j].game)];
      for (int s = 0; s < g.positions.front().player_count; ++s) {
        by_game[at(jobs[j].game)].push_back({counts[j].share(s), s == g.winner ? 1 : 0});
      }
    }
    positions = static_cast<long>(jobs.size());
    std::printf("Monte Carlo, %d playouts per position, %s bots, on %zu games\n", o.playouts,
                o.record_bots ? "the game's own" : "random zoo", set.size());
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("%ld positions in %.1f s\n\n", positions, seconds);
  cae::print_calibration(cae::calibrate(by_game, o.bootstrap, 12345), set.front().positions.front().player_count);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) usage();
  const std::string command = argv[1];
  Options o;
  for (int i = 2; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage();
      return argv[++i];
    };
    if (flag == "--games") {
      o.games = std::atoi(value().c_str());
    } else if (flag == "--seed") {
      o.seed = std::stoull(value());
    } else if (flag == "--positions") {
      o.positions = std::atoi(value().c_str());
    } else if (flag == "--threads") {
      o.threads = std::max(1, std::atoi(value().c_str()));
    } else if (flag == "--playouts") {
      o.playouts = std::atoi(value().c_str());
    } else if (flag == "--bots") {
      o.record_bots = value() == "record";
    } else if (flag == "--bot") {
      o.bot = value();
    } else if (flag == "--model") {
      o.model = value();
    } else if (flag == "--bootstrap") {
      o.bootstrap = std::atoi(value().c_str());
    } else {
      usage();
    }
  }
  try {
    if (command == "make") return make(o);
    if (command == "train") return train(o);
    if (command == "eval") return eval(o);
    if (command == "sanity") return sanity(o, o.bot);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "catan_validate: %s\n", e.what());
    return 1;
  }
  usage();
}
