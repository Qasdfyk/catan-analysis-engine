#include "catan/mcts.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  return a * 0x9e3779b97f4a7c15ULL + b + 0x632be59bd9b4e019ULL;
}

Action make(ActionKind kind, int target = -1) {
  Action a;
  a.kind = kind;
  a.target = target;
  return a;
}

Action resources(ActionKind kind, Resource first, Resource second = Resource::kWood) {
  Action a = make(kind);
  a.first = first;
  a.second = second;
  return a;
}

bool has_card(std::uint8_t playable, DevCard c) { return (playable & (1U << static_cast<unsigned>(c))) != 0; }

// One node of the tree: the moves that led to it, and its statistics.
struct Node {
  Action action;  // the move from the parent
  double prior = 0;
  int visits = 0;
  double wins = 0;
  int available = 0;
  std::vector<int> children;
};

}  // namespace

bool searched_decision(const Game& game, int seat) {
  if (game.game_over || game.current != seat || next_actor(game) != seat) return false;
  switch (game.phase) {
    case Phase::kSetup:
    case Phase::kMoveRobber:
    case Phase::kSteal:
    case Phase::kRoadBuilding: return true;
    case Phase::kRollDice: return has_card(game.playable_dev_cards(seat), DevCard::kKnight);
    case Phase::kMain: return !game.offer.open;
    case Phase::kDiscard: return false;
  }
  return false;
}

std::vector<Action> search_candidates(const Game& game, int seat, const std::optional<Action>& zoo_move,
                                      bool trades) {
  std::vector<Action> out;
  if (!searched_decision(game, seat)) return out;
  const Player& me = game.players[at(seat)];
  const auto settlements = [&] {
    for_each_node(game.legal_settlements(seat), [&](int n) { out.push_back(make(ActionKind::kBuildSettlement, n)); });
  };
  const auto roads = [&] {
    for_each_edge(game.legal_roads(seat), [&](int e) { out.push_back(make(ActionKind::kBuildRoad, e)); });
  };
  const std::uint8_t playable = game.playable_dev_cards(seat);
  switch (game.phase) {
    case Phase::kSetup:
      settlements();
      roads();
      break;
    case Phase::kRollDice:
      out.push_back(make(ActionKind::kRollDice));
      out.push_back(make(ActionKind::kPlayKnight));
      break;
    case Phase::kMoveRobber: {
      const std::uint32_t hexes = game.legal_robber_hexes(seat);
      for (int h = 0; h < kHexCount; ++h) {
        if ((hexes >> h) & 1U) out.push_back(make(ActionKind::kMoveRobber, h));
      }
      break;
    }
    case Phase::kSteal: {
      const std::uint8_t victims = game.steal_victims(seat);
      for (int s = 0; s < game.player_count; ++s) {
        if ((victims >> s) & 1U) out.push_back(make(ActionKind::kSteal, s));
      }
      break;
    }
    case Phase::kRoadBuilding: roads(); break;
    case Phase::kMain: {
      out.push_back(make(ActionKind::kEndTurn));
      settlements();
      for_each_node(game.legal_cities(seat), [&](int n) { out.push_back(make(ActionKind::kUpgradeCity, n)); });
      roads();
      if (game.can_buy_dev_card(seat)) out.push_back(make(ActionKind::kBuyDevCard));
      if (has_card(playable, DevCard::kKnight)) out.push_back(make(ActionKind::kPlayKnight));
      if (has_card(playable, DevCard::kRoadBuilding)) out.push_back(make(ActionKind::kPlayRoadBuilding));
      for (int a = 0; a < kResourceCount; ++a) {
        const auto ra = static_cast<Resource>(a);
        if (has_card(playable, DevCard::kMonopoly)) out.push_back(resources(ActionKind::kPlayMonopoly, ra));
        if (has_card(playable, DevCard::kYearOfPlenty)) {
          for (int b = a; b < kResourceCount; ++b) {
            const auto rb = static_cast<Resource>(b);
            const int need = a == b ? 2 : 1;
            if (game.bank_count(ra) >= need && game.bank_count(rb) >= need) {
              out.push_back(resources(ActionKind::kPlayYearOfPlenty, ra, rb));
            }
          }
        }
      }
      for (int give = 0; give < kResourceCount; ++give) {
        const auto rg = static_cast<Resource>(give);
        if (me.resources[at(give)] < game.trade_rate(seat, rg)) continue;
        for (int get = 0; get < kResourceCount; ++get) {
          const auto rt = static_cast<Resource>(get);
          if (get != give && game.bank_count(rt) > 0) out.push_back(resources(ActionKind::kTradeWithBank, rg, rt));
        }
      }
      break;
    }
    case Phase::kDiscard: break;
  }
  if (zoo_move && std::find(out.begin(), out.end(), *zoo_move) == out.end()) {
    if (trades || zoo_move->kind != ActionKind::kOfferTrade) out.push_back(*zoo_move);
  }
  return out;
}

MctsSearch mcts_search(const Game& game, int seat, const std::array<Cards, kMaxPlayers>& known,
                       const TableMemory& memory, const ZooBot& self, std::span<const ZooBot> opponents,
                       const MctsSettings& settings, std::uint64_t seed) {
  std::vector<Node> tree(1);
  tree.reserve(static_cast<std::size_t>(settings.iterations) + 64);
  const ZooBot hard = hard_bot();
  std::vector<Action> candidates;
  std::vector<int> legal;
  std::vector<int> path;

  const bool timed = settings.deadline != std::chrono::steady_clock::time_point{};
  int done = 0;
  for (int it = 0; it < settings.iterations; ++it) {
    if (timed && it > 0 && std::chrono::steady_clock::now() >= settings.deadline) break;
    ++done;
    Rng rng(mix(seed, static_cast<std::uint64_t>(it)));
    // 1. A state that agrees with what the seat knows.
    ZooTable table;
    table.game = settings.peek ? game : determinize(game, seat, known, rng);
    table.memory = memory;
    Game& g = table.game;

    // 2-3. Down the tree while it is this seat's searched decision.
    int node = 0;
    path.assign(1, 0);
    while (searched_decision(g, seat)) {
      table.memory.sync(g);
      const std::optional<Action> zoo_move = zoo_decide(g, seat, self, table.memory);
      candidates = search_candidates(g, seat, zoo_move, settings.trades);
      if (candidates.empty()) break;
      // Children for moves seen for the first time; every legal child
      // counts one more availability.
      legal.clear();
      const double other_prior =
          candidates.size() > 1 ? (1.0 - settings.zoo_prior) / static_cast<double>(candidates.size() - 1) : 1.0;
      for (const Action& a : candidates) {
        int found = -1;
        for (const int c : tree[at(node)].children) {
          if (tree[at(c)].action == a) {
            found = c;
            break;
          }
        }
        if (found < 0) {
          Node child;
          child.action = a;
          child.prior = candidates.size() == 1 ? 1.0 : (zoo_move && a == *zoo_move ? settings.zoo_prior : other_prior);
          found = static_cast<int>(tree.size());
          tree.push_back(std::move(child));
          tree[at(node)].children.push_back(found);
        }
        ++tree[at(found)].available;
        legal.push_back(found);
      }
      // PUCT among the legal children.
      const Node& parent = tree[at(node)];
      const double parent_q = parent.visits > 0 ? parent.wins / parent.visits : 0.25;
      int pick = legal.front();
      double best = -1e300;
      for (const int c : legal) {
        const Node& ch = tree[at(c)];
        const double q = ch.visits > 0 ? ch.wins / ch.visits : parent_q;
        const double u = settings.exploration * ch.prior * std::sqrt(static_cast<double>(ch.available)) /
                         (1.0 + ch.visits);
        if (q + u > best) {
          best = q + u;
          pick = c;
        }
      }
      const Action move = tree[at(pick)].action;
      if (apply_action(g, seat, move, rng) != Error::kNone) break;  // not legal after all: play out from here
      table.memory.record(seat, move);
      node = pick;
      path.push_back(node);
      if (tree[at(node)].visits == 0) break;
    }

    // 4. Play out with zoo bots.
    for (int s = 0; s < g.player_count; ++s) {
      if (s == seat) {
        table.bots[at(s)] = self;
      } else {
        table.bots[at(s)] = opponents.empty() ? hard : opponents[at(rng.below(static_cast<int>(opponents.size())))];
        if (!settings.trades) table.bots[at(s)].trading.offers_per_turn = 0;
      }
    }
    const ZooPlayout r = g.game_over ? ZooPlayout{g.winner, 0, 0} : play_out_zoo(table, rng);
    const double win = r.winner == seat ? 1.0 : 0.0;

    // 5. Back up.
    for (const int n : path) {
      ++tree[at(n)].visits;
      tree[at(n)].wins += win;
    }
  }

  MctsSearch out;
  out.iterations = done;
  out.nodes = static_cast<int>(tree.size());
  int most = -1;
  for (const int c : tree.front().children) {
    const Node& ch = tree[at(c)];
    out.root.push_back({ch.action, ch.prior, ch.visits, ch.wins, ch.available});
    if (ch.visits > most) {
      most = ch.visits;
      out.best = ch.action;
    }
  }
  return out;
}

void merge_searches(MctsSearch& into, const MctsSearch& other) {
  for (const MctsMove& m : other.root) {
    auto it = std::find_if(into.root.begin(), into.root.end(), [&](const MctsMove& x) { return x.action == m.action; });
    if (it == into.root.end()) {
      into.root.push_back(m);
    } else {
      it->visits += m.visits;
      it->wins += m.wins;
      it->available += m.available;
    }
  }
  into.iterations += other.iterations;
  into.nodes += other.nodes;
  int most = -1;
  for (const MctsMove& m : into.root) {
    if (m.visits > most) {
      most = m.visits;
      into.best = m.action;
    }
  }
}

std::optional<Action> mcts_decide(const ArenaView& view, const ZooBot& self, std::span<const ZooBot> opponents,
                                  const MctsSettings& settings, std::uint64_t seed, MctsSearch* out) {
  std::optional<Action> zoo_move = zoo_decide(view.game, view.seat, self, view.memory);
  if (!settings.trades && zoo_move && zoo_move->kind == ActionKind::kRespondTrade) zoo_move->accept = false;
  const std::vector<Action> candidates = search_candidates(view.game, view.seat, zoo_move, settings.trades);
  if (candidates.size() <= 1) return candidates.empty() ? zoo_move : std::optional<Action>(candidates.front());
  MctsSearch search =
      mcts_search(view.game, view.seat, view.belief.known[at(view.seat)], view.memory, self, opponents, settings, seed);
  const Action best = search.best;
  if (out != nullptr) *out = std::move(search);
  return best;
}

ArenaPlayer mcts_player(const ZooBot& self, std::vector<ZooBot> opponents, MctsSettings settings, std::uint64_t seed) {
  ZooBot me = self;
  if (!settings.trades) me.trading.offers_per_turn = 0;
  auto decisions = std::make_shared<std::uint64_t>(0);
  auto pool = std::make_shared<const std::vector<ZooBot>>(std::move(opponents));
  return [me, pool, settings, seed, decisions](const ArenaView& v) -> std::optional<Action> {
    return mcts_decide(v, me, *pool, settings, mix(seed, (*decisions)++));
  };
}

std::vector<RecordDecision> record_decisions(std::span<const Game> states) {
  std::vector<RecordDecision> out;
  HandBelief belief;
  TableMemory memory;
  for (std::size_t i = 0; i < states.size(); ++i) {
    const Game& g = states[i];
    if (i > 0) {
      const Game& before = states[i - 1];
      belief.observe(before, g);
      memory.sync(g);
      for (int s = 0; s < g.player_count; ++s) {
        for_each_node(g.players[at(s)].settlements & ~before.players[at(s)].settlements,
                      [&](int n) { memory.record(s, make(ActionKind::kBuildSettlement, n)); });
      }
    }
    memory.sync(g);
    const int seat = next_actor(g);
    if (seat < 0 || i + 1 == states.size() || !searched_decision(g, seat)) continue;
    if (search_candidates(g, seat, std::nullopt).size() <= 1) continue;
    out.push_back({static_cast<int>(i), seat, belief.known[at(seat)], memory});
  }
  return out;
}

std::vector<RecordDecision> spread_decisions(std::span<const Game> states, int count) {
  std::vector<RecordDecision> all = record_decisions(states);
  if (count <= 0 || static_cast<int>(all.size()) <= count) return all;
  std::vector<RecordDecision> out;
  for (int k = 0; k < count; ++k) {
    out.push_back(all[at(static_cast<int>((static_cast<long>(k) * static_cast<long>(all.size())) / count))]);
  }
  return out;
}

MctsSearch search_part(const Game& state, const RecordDecision& d, std::span<const ZooBot> opponents,
                       const MctsSettings& settings, std::uint64_t seed, int worker) {
  return mcts_search(state, d.seat, d.known, d.memory, hard_bot(), opponents, settings,
                     mix(seed, static_cast<std::uint64_t>(worker)));
}

std::string action_text(const Action& a) {
  std::string out(action_name(a.kind));
  switch (a.kind) {
    case ActionKind::kBuildSettlement:
    case ActionKind::kBuildRoad:
    case ActionKind::kUpgradeCity:
    case ActionKind::kMoveRobber:
    case ActionKind::kSteal:
    case ActionKind::kConfirmTrade: out += " " + std::to_string(a.target); break;
    case ActionKind::kTradeWithBank:
    case ActionKind::kPlayYearOfPlenty:
      out += " " + std::string(resource_name(a.first)) + " " + std::string(resource_name(a.second));
      break;
    case ActionKind::kPlayMonopoly: out += " " + std::string(resource_name(a.first)); break;
    case ActionKind::kOfferTrade: {
      const auto cards = [](const Cards& c) {
        std::string t;
        for (int r = 0; r < kResourceCount; ++r) {
          for (int k = 0; k < c[at(r)]; ++k) t += std::string(t.empty() ? "" : "+") + std::string(resource_name(static_cast<Resource>(r)));
        }
        return t;
      };
      out += " " + cards(a.give) + " for " + cards(a.get);
      break;
    }
    default: break;
  }
  return out;
}

}  // namespace cae
