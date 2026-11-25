#include "catan/review.hpp"

#include "catan/mcts.hpp"
#include "catan/record.hpp"
#include "catan/belief.hpp"
#include "catan/bits.hpp"
#include "catan/rng.hpp"
#include "catan/topology.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace cae {

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// Standard error of the mean of x[i] - y[i] over the pairs.
double paired_error(const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y) noexcept {
  const std::size_t n = std::min(x.size(), y.size());
  if (n < 2) return 0;
  double sum = 0;
  double squares = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double d = static_cast<double>(x[i]) - static_cast<double>(y[i]);
    sum += d;
    squares += d * d;
  }
  const double count = static_cast<double>(n);
  const double mean = sum / count;
  const double variance = (squares - count * mean * mean) / (count - 1);
  return std::sqrt(std::max(variance, 0.0) / count);
}

double binomial_variance(const WinCounts& w, int seat) noexcept {
  if (w.playouts == 0) return 0;
  const double p = w.share(seat);
  return p * (1 - p) / w.playouts;
}

// How many of the 36 rolls of two dice give `number` (0 for the desert).
constexpr int pips(int number) noexcept { return number < 2 || number > 12 || number == 7 ? 0 : 6 - std::abs(7 - number); }

// Pips the robber on `hex` takes from each seat: the hex's pips once per
// settlement and twice per city on its corners.
std::array<int, kMaxPlayers> blocked(const Game& g, int hex) {
  std::array<int, kMaxPlayers> out{};
  if (hex < 0 || hex >= kHexCount) return out;
  const int p = pips(g.board.number[at(hex)]);
  for (int s = 0; s < g.player_count; ++s) {
    const Player& pl = g.players[at(s)];
    const NodeMask corners = kTopology.hex_node_mask[at(hex)];
    out[at(s)] = p * (std::popcount(pl.settlements & corners) + 2 * std::popcount(pl.cities & corners));
  }
  return out;
}

// The pips a building on `node` collects, and the numbers around it.
int node_pips(const Game& g, int node, json* numbers = nullptr) {
  int total = 0;
  for (const std::int8_t h : kTopology.node_hexes[at(node)]) {
    if (h < 0) continue;
    const int n = g.board.number[at(h)];
    if (n == 0) continue;
    total += pips(n);
    if (numbers != nullptr) numbers->push_back(n);
  }
  return total;
}

// The opponent of `seat` with the most points in sight, -1 if none.
int leader_of(const Game& g, int seat) {
  int leader = -1;
  for (int s = 0; s < g.player_count; ++s) {
    if (s == seat) continue;
    if (leader < 0 || g.visible_points(s) > g.visible_points(leader)) leader = s;
  }
  return leader;
}

// The position after `seat` makes `a`; chance (a steal) from a fixed seed,
// so the same review gives the same facts.
Game after(const Game& g, int seat, const Action& a) {
  Game next = g;
  Rng rng(1);
  (void)apply_action(next, seat, a, rng);
  return next;
}

// What `seat` can pay for and place right now, as names.
json affordable(const Game& g, int seat) {
  json out = json::array();
  if (g.legal_cities(seat) != 0) out.push_back("city");
  if (g.legal_settlements(seat) != 0) out.push_back("settlement");
  if (g.can_buy_dev_card(seat)) out.push_back("development card");
  if (g.legal_roads(seat).any()) out.push_back("road");
  return out;
}

// Corners where `seat` could settle if it had the cards: the reach of its roads.
NodeMask settle_spots(const Game& g, int seat) {
  if (g.phase != Phase::kMain || g.current != seat) return 0;
  Game rich = g;
  for (std::size_t r = 0; r < kSettlementCost.size(); ++r) {
    rich.players[at(seat)].resources[r] = static_cast<std::int8_t>(std::max<int>(rich.players[at(seat)].resources[r], kSettlementCost[r]));
  }
  return rich.legal_settlements(seat);
}

// The port at a corner: 0 for 3:1, 1 + resource for 2:1, -1 for none.
int port_at(const Game& g, int node) {
  for (int i = 0; i < g.board.port_count; ++i) {
    if (contains(g.board.ports[at(i)].nodes, node)) return static_cast<int>(g.board.ports[at(i)].type);
  }
  return -1;
}

// Whether `seat` already trades at a port of this type.
bool has_port(const Game& g, int seat, int type) {
  for (int i = 0; i < g.board.port_count; ++i) {
    const Port& p = g.board.ports[at(i)];
    if (static_cast<int>(p.type) == type && (p.nodes & g.players[at(seat)].buildings()) != 0) return true;
  }
  return false;
}

bool builds(ActionKind k) noexcept {
  return k == ActionKind::kBuildSettlement || k == ActionKind::kUpgradeCity || k == ActionKind::kBuildRoad ||
         k == ActionKind::kBuyDevCard;
}

}  // namespace

std::string_view grade_name(Grade g) noexcept {
  switch (g) {
    case Grade::kBest: return "best";
    case Grade::kGood: return "good";
    case Grade::kInaccuracy: return "inaccuracy";
    case Grade::kMistake: return "mistake";
    case Grade::kBlunder: return "blunder";
  }
  return "?";
}

std::vector<ReviewDecision> review_decisions(const json& record, int seat, bool setup) {
  std::vector<ReviewDecision> out;
  TablePosition p;
  p.game = game_from_record(record);
  if (!record.contains("events") || !record["events"].is_array()) throw RecordError("record has no events");
  for (const json& event : record["events"]) {
    const Game& g = p.game;
    const int actor = event.value("seat", -1);
    if (!g.game_over && actor >= 0 && (seat < 0 || actor == seat) && (setup || g.phase != Phase::kSetup) &&
        searched_decision(g, actor)) {
      const std::optional<Action> played = action_from_event(event);
      if (played && search_candidates(g, actor, std::nullopt).size() > 1) {
        p.memory.sync(g);
        out.push_back({p.events, actor, g, p.belief.known[at(actor)], p.memory, *played});
      }
    }
    apply_record_event(p, event);
  }
  return out;
}

Grade grade_of(double loss, double error, bool played_is_best, const GradeThresholds& t) noexcept {
  if (played_is_best) return Grade::kBest;
  if (loss < t.z * error || loss < t.inaccuracy) return Grade::kGood;
  if (loss < t.mistake) return Grade::kInaccuracy;
  if (loss < t.blunder) return Grade::kMistake;
  return Grade::kBlunder;
}

MoveReview review_decision(const ReviewDecision& d, const ReviewConfig& config, std::span<const ZooBot> pool) {
  const GoldenLabel label = label_position(d.game, d.seat, d.known, d.memory, d.played, config.label, pool);
  MoveReview r;
  r.state = d.state;
  r.seat = d.seat;
  r.turn = d.game.turn_number;
  r.played = d.played;
  r.candidates = static_cast<int>(label.moves.size());
  r.playouts = label.playouts;
  if (label.best < 0 || label.played < 0) {
    r.best = d.played;
    return r;
  }
  const GoldenMove& best = label.moves[at(label.best)];
  const GoldenMove& played = label.moves[at(label.played)];
  r.best = best.action;
  r.best_value = golden_value(best, d.seat);
  r.played_value = golden_value(played, d.seat);
  const bool same = label.best == label.played;
  if (!same) {
    r.loss = r.best_value - r.played_value;
    r.error = paired_error(best.confirm_won, played.confirm_won);
    r.error_unpaired = std::sqrt(binomial_variance(best.confirm, d.seat) + binomial_variance(played.confirm, d.seat));
  }
  const auto now = [](const GoldenMove& m) {
    return m.confirm.playouts > 0 ? static_cast<double>(m.confirm_now) / m.confirm.playouts : 0.0;
  };
  r.best_now = now(best);
  r.played_now = now(played);
  r.grade = grade_of(r.loss, r.error, same, config.thresholds);
  r.within_noise = !same && r.loss < config.thresholds.z * r.error;
  return r;
}

json explain(const ReviewDecision& d, const MoveReview& r) {
  if (r.grade < Grade::kInaccuracy) return nullptr;
  const int seat = d.seat;
  const Game& g = d.game;
  const Action& best = r.best;
  const Action& played = r.played;

  if (r.best_now >= 0.5 && r.played_now <= r.best_now - 0.25) {
    return {{"kind", "wins_now"}, {"best_now", r.best_now}, {"played_now", r.played_now}};
  }
  if (best.kind == ActionKind::kMoveRobber && played.kind == ActionKind::kMoveRobber) {
    const auto b = blocked(g, best.target);
    const auto p = blocked(g, played.target);
    return {{"kind", "robber"},
            {"best_hex", best.target},
            {"played_hex", played.target},
            {"best_blocks", std::vector<int>(b.begin(), b.begin() + g.player_count)},
            {"played_blocks", std::vector<int>(p.begin(), p.begin() + g.player_count)},
            {"leader", leader_of(g, seat)}};
  }
  if (best.kind == ActionKind::kSteal && played.kind == ActionKind::kSteal) {
    json points = json::array();
    json cards = json::array();
    for (int s = 0; s < g.player_count; ++s) {
      points.push_back(g.visible_points(s));
      cards.push_back(g.players[at(s)].card_count());
    }
    return {{"kind", "victim"},
            {"best_victim", best.target},
            {"played_victim", played.target},
            {"points", points},
            {"cards", cards}};
  }
  const Game gb = after(g, seat, best);
  const Game gp = after(g, seat, played);
  if (gb.longest_road == seat && gp.longest_road != seat && g.longest_road != seat) return {{"kind", "longest_road"}};
  if (gb.largest_army == seat && gp.largest_army != seat && g.largest_army != seat) return {{"kind", "largest_army"}};
  if (gb.visible_points(seat) > gp.visible_points(seat)) {
    return {{"kind", "points"}, {"best_points", gb.visible_points(seat)}, {"played_points", gp.visible_points(seat)}};
  }
  const auto building = [](const Action& a) {
    return a.kind == ActionKind::kBuildSettlement || a.kind == ActionKind::kUpgradeCity;
  };
  if (building(best)) {
    json numbers = json::array();
    const int pips_best = node_pips(g, best.target, &numbers);
    const int pips_played = building(played) ? node_pips(g, played.target) : 0;
    if (pips_best > pips_played) {
      return {{"kind", "production"},
              {"node", best.target},
              {"city", best.kind == ActionKind::kUpgradeCity},
              {"pips", pips_best},
              {"numbers", numbers},
              {"played_pips", pips_played}};
    }
  }
  // A settlement on a port the player has none of, where the move played gets none.
  if (best.kind == ActionKind::kBuildSettlement) {
    const int port = port_at(g, best.target);
    const int played_port = played.kind == ActionKind::kBuildSettlement ? port_at(g, played.target) : -1;
    if (port >= 0 && port != played_port && !has_port(g, seat, port)) {
      return {{"kind", "port"},
              {"node", best.target},
              {"port", port == 0 ? std::string("3:1") : std::string(resource_name(static_cast<Resource>(port - 1)))}};
    }
  }
  // A trade or a card that pays for something the move played does not.
  const json can_best = affordable(gb, seat);
  const json can_played = affordable(gp, seat);
  json gained = json::array();
  for (const json& b : can_best) {
    if (std::find(can_played.begin(), can_played.end(), b) == can_played.end()) gained.push_back(b);
  }
  const bool card_or_trade = best.kind == ActionKind::kTradeWithBank || best.kind == ActionKind::kConfirmTrade ||
                             best.kind == ActionKind::kPlayYearOfPlenty || best.kind == ActionKind::kPlayMonopoly;
  if (card_or_trade && !gained.empty()) return {{"kind", "enables"}, {"builds", gained}};
  if (best.kind == ActionKind::kBuildRoad) {
    // A road that reaches a corner to settle on, the best such corner.
    const NodeMask fresh = settle_spots(gb, seat) & ~settle_spots(gp, seat) & ~settle_spots(g, seat);
    int spot = -1;
    for_each_node(fresh, [&](int n) {
      if (spot < 0 || node_pips(g, n) > node_pips(g, spot)) spot = n;
    });
    if (spot >= 0) {
      json numbers = json::array();
      const int p = node_pips(g, spot, &numbers);
      return {{"kind", "road_spot"}, {"node", spot}, {"pips", p}, {"numbers", numbers}};
    }
    const int lb = gb.road_lengths[at(seat)];
    const int lp = gp.road_lengths[at(seat)];
    if (lb > lp) {
      const int holder = g.longest_road;
      return {{"kind", "road_length"},
              {"best_length", lb},
              {"played_length", lp},
              {"holder", holder},
              {"holder_length", holder >= 0 ? g.road_lengths[at(holder)] : 0}};
    }
  }
  if (played.kind == ActionKind::kEndTurn && builds(best.kind)) return {{"kind", "ended_turn"}};
  if (best.kind == ActionKind::kBuyDevCard) {
    // The deck as the player saw it: every card not played and not in hand.
    const auto played_cards = played_dev_cards(g);
    const Player& me = g.players[at(seat)];
    std::array<int, kDevCardKinds> unseen{};
    int left = 0;
    for (std::size_t c = 0; c < unseen.size(); ++c) {
      unseen[c] = kDevDeckComposition[c] - played_cards[c] - me.dev_cards[c] - me.new_dev_cards[c];
      left += unseen[c];
    }
    const int holder = g.largest_army;
    return {{"kind", "dev_card"},
            {"unseen", left},
            {"points", unseen[static_cast<std::size_t>(DevCard::kVictoryPoint)]},
            {"knights", unseen[static_cast<std::size_t>(DevCard::kKnight)]},
            {"my_knights", me.knights},
            {"holder", holder},
            {"holder_knights", holder >= 0 ? g.players[at(holder)].knights : 0}};
  }
  return {{"kind", "later"}};
}

json review_json(const MoveReview& r) {
  return {{"state", r.state},
          {"seat", r.seat},
          {"turn", r.turn},
          {"played", action_json(r.played)},
          {"best", action_json(r.best)},
          {"played_value", r.played_value},
          {"best_value", r.best_value},
          {"loss", r.loss},
          {"error", r.error},
          {"error_unpaired", r.error_unpaired},
          {"grade", grade_name(r.grade)},
          {"within_noise", r.within_noise},
          {"best_now", r.best_now},
          {"played_now", r.played_now},
          {"candidates", r.candidates},
          {"playouts", r.playouts}};
}

ReviewSummary summarize(std::span<const MoveReview> reviews) {
  ReviewSummary s;
  double total = 0;
  for (const MoveReview& r : reviews) {
    ++s.decisions;
    ++s.grades[static_cast<std::size_t>(r.grade)];
    total += std::max(r.loss, 0.0);
  }
  if (s.decisions > 0) s.mean_loss = total / s.decisions;
  return s;
}

}  // namespace cae
