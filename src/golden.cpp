#include "catan/golden.hpp"

#include "catan/mcts.hpp"
#include "catan/rng.hpp"

#include <algorithm>
#include <numeric>

namespace cae {

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  return a * 0x9e3779b97f4a7c15ULL + b + 0x632be59bd9b4e019ULL;
}

// Playout indices of the confirmation, far above those of the halving.
constexpr int kConfirmFirst = 1 < 24;

}  // namespace

double golden_value(const GoldenMove& m, int seat) noexcept {
  return m.confirm.playouts > 0 ? m.confirm.share(seat) : m.halving.share(seat);
}

GoldenLabel label_position(const Game& truth, int seat, const std::array<Cards, kMaxPlayers>& known,
                           const TableMemory& memory, const std::optional<Action>& played,
                           const GoldenConfig& config, std::span<const ZooBot> pool) {
  GoldenLabel label;
  const ZooBot hard = hard_bot();
  TableMemory synced = memory;
  synced.sync(truth);
  const std::optional<Action> zoo_move = zoo_decide(truth, seat, hard, synced);
  for (const Action& a : search_candidates(truth, seat, zoo_move)) label.moves.push_back({a, {}, {}, false});
  if (played) {
    const auto it = std::find_if(label.moves.begin(), label.moves.end(),
                                 [&](const GoldenMove& m) { return m.action == *played; });
    if (it == label.moves.end()) label.moves.push_back({*played, {}, {}, false});
    label.played = static_cast<int>(std::find_if(label.moves.begin(), label.moves.end(),
                                                 [&](const GoldenMove& m) { return m.action == *played; }) -
                                    label.moves.begin());
  }
  if (label.moves.empty()) return label;

  // Playouts first .. first + count - 1 of one move.
  const auto play = [&](GoldenMove& m, int first, int count, WinCounts& into) {
    for (int i = first; i < first + count; ++i) {
      Rng rng(mix(config.seed, static_cast<std::uint64_t>(i)));
      ZooTable t;
      t.game = determinize(truth, seat, known, rng);
      t.memory = synced;
      for (int s = 0; s < t.game.player_count; ++s) {
        t.bots[at(s)] = s == seat || pool.empty() ? hard : pool[at(rng.below(static_cast<int>(pool.size())))];
      }
      int winner = -1;
      if (apply_action(t.game, seat, m.action, rng) == Error::kNone) {
        t.memory.record(seat, m.action);
        winner = t.game.game_over ? t.game.winner : play_out_zoo(t, rng).winner;
      } else {
        // Not legal in this draw (it cannot happen for the seat's own moves,
        // which depend only on what it sees); counts as not won.
        winner = -1;
      }
      ++into.playouts;
      if (winner >= 0) {
        ++into.wins[at(winner)];
      } else {
        ++into.unfinished;
      }
      ++label.playouts;
    }
  };
  const auto value = [&](int i) { return label.moves[at(i)].halving.share(seat); };
  const auto better = [&](int a, int b) { return value(a) != value(b) ? value(a) > value(b) : a < b; };

  // Halving among the contenders; the move played rides along in every
  // round and is never dropped, since it is the one a review grades.
  std::vector<int> contenders;
  for (int i = 0; i < static_cast<int>(label.moves.size()); ++i) {
    if (i != label.played) contenders.push_back(i);
  }
  int first = 0;
  int count = config.first_playouts;
  while (true) {
    for (const int i : contenders) play(label.moves[at(i)], first, count, label.moves[at(i)].halving);
    if (label.played >= 0) play(label.moves[at(label.played)], first, count, label.moves[at(label.played)].halving);
    first += count;
    std::sort(contenders.begin(), contenders.end(), better);
    if (static_cast<int>(contenders.size()) <= config.finalists) break;
    contenders.resize(at(std::max(config.finalists, static_cast<int>(contenders.size()) / 2)));
    count *= 2;
  }
  std::vector<int> alive = contenders;
  if (label.played >= 0) alive.push_back(label.played);
  std::sort(alive.begin(), alive.end(), better);
  for (std::size_t k = 0; k < alive.size(); ++k) {
    const int i = alive[k];
    if (static_cast<int>(k) < config.finalists || i == label.played) {
      label.moves[at(i)].finalist = true;
      play(label.moves[at(i)], kConfirmFirst, config.confirm_playouts, label.moves[at(i)].confirm);
    }
  }
  label.best = alive.front();
  return label;
}

json memory_json(const TableMemory& m) {
  json built = json::array();
  for (int i = 0; i < m.built_count; ++i) built.push_back(m.built[at(i)]);
  json accepted = json::array();
  for (const auto a : m.accepted) accepted.push_back(a);
  return {{"turn", m.turn},
          {"offers_made", m.offers_made},
          {"accepted", accepted},
          {"accepted_count", m.accepted_count},
          {"built", built}};
}

TableMemory memory_from_json(const json& j) {
  TableMemory m;
  m.turn = j.at("turn").get<int>();
  m.offers_made = j.at("offers_made").get<int>();
  for (std::size_t i = 0; i < m.accepted.size(); ++i) m.accepted[i] = j.at("accepted").at(i).get<std::int8_t>();
  m.accepted_count = j.at("accepted_count").get<int>();
  for (const json& n : j.at("built")) m.built[at(m.built_count++)] = n.get<std::int8_t>();
  return m;
}

}  // namespace cae
