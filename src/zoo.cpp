#include "catan/zoo.hpp"

#include "catan/random_player.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

// Every function below names the Go function it ports. The Go bot reads a
// JSON view of the game and compares positions by their coordinates; here
// the same questions are asked of the Game struct by index. Where the Go
// code depends on an order (the first of equal options wins), the order is
// reproduced: legal places in the server's order, corners by coordinates,
// tiles by hex index. Floating point sums are written in the same order as
// in Go, so they round the same way.

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }
constexpr Resource res(int i) noexcept { return static_cast<Resource>(i); }

// cardsOfEach: how many cards of each resource the game has.
constexpr int kCardsOfEach = kBankSize;
// targetReach: how many roads ahead the bot looks for a settlement spot.
constexpr int kTargetReach = 3;
// devCardValue: a development card against a building's 1 + production.
constexpr double kDevCardValue = 0.3;
// jitterSpread: standard deviation of the log of each weight's factor.
constexpr double kJitterSpread = 0.2;

// A list of at most N small values, in order, without the heap.
template <std::size_t N>
struct List {
  std::array<std::int8_t, N> items{};
  int size = 0;
  void push(int v) noexcept { items[at(size++)] = static_cast<std::int8_t>(v); }
  [[nodiscard]] bool empty() const noexcept { return size == 0; }
  [[nodiscard]] int operator[](int i) const noexcept { return items[at(i)]; }
  [[nodiscard]] const std::int8_t* begin() const noexcept { return items.data(); }
  [[nodiscard]] const std::int8_t* end() const noexcept { return items.data() + size; }
};

using NodeList = List<kNodeCount>;
using EdgeList = List<kEdgeCount>;

// pips: how many of the 36 dice combinations roll the number.
constexpr int pips(int number) noexcept {
  if (number == 0) return 0;
  return number > 7 ? 13 - number : number - 1;
}

int total(const Cards& cards) noexcept { return std::accumulate(cards.begin(), cards.end(), 0); }

// missing: the cards of `cost` the hand lacks.
int missing(const Cards& cost, const Cards& hand) noexcept {
  int n = 0;
  for (int r = 0; r < kResourceCount; ++r) n += std::max(0, cost[at(r)] - hand[at(r)]);
  return n;
}

Cards pay(Cards hand, const Cards& cost) noexcept {
  for (int r = 0; r < kResourceCount; ++r) hand[at(r)] -= cost[at(r)];
  return hand;
}

// --- Orders ---

// The corners sorted the way board.go sorts them: by their coordinates
// (x, then y) in hundredths of a hex side. findTarget() takes the first of
// equally good corners in this order.
struct CornerOrder {
  std::array<std::int8_t, kNodeCount> nodes{};
  CornerOrder() {
    std::array<std::array<long, 2>, kNodeCount> key{};
    for (int n = 0; n < kNodeCount; ++n) {
      const Spelling& s = kTopology.node_spelling[at(n)];
      // Hex.center() and Position.vertex() in view.go
      const double cx = std::sqrt(3.0) * (static_cast<double>(s.hex.q) + static_cast<double>(s.hex.r) / 2);
      const double cy = 1.5 * static_cast<double>(s.hex.r);
      const double angle = (static_cast<double>(60 * s.direction) - 30) * 3.14159265358979323846 / 180;
      key[at(n)] = {std::lround((cx + std::cos(angle)) * 100), std::lround((cy + std::sin(angle)) * 100)};
      nodes[at(n)] = static_cast<std::int8_t>(n);
    }
    std::sort(nodes.begin(), nodes.end(), [&](std::int8_t a, std::int8_t b) { return key[at(a)] < key[at(b)]; });
  }
};

const CornerOrder& corner_order() {
  static const CornerOrder order;
  return order;
}

// Board.vertices() and Board.edges() of catan-project iterated as Python
// iterates them, written as canonical (q, r, direction) spellings. Printed by
//   Board.create_standard_game().vertices() / .edges()
// in catan-project's backend; see server_node_order() in zoo.hpp.
constexpr std::array<std::array<int, 3>, kNodeCount> kServerNodes = {{
    {-2, -1, 1}, {-2, 2, 1}, {-1, -2, 1}, {1, 1, 0}, {-2, 0, 1}, {-2, 3, 0}, {-1, 3, 0}, {2, -2, 1},
    {-3, 2, 0}, {-1, 2, 1}, {1, 0, 0}, {1, -3, 1}, {0, -2, 0}, {1, -1, 1}, {0, 1, 1}, {1, -2, 0},
    {2, -1, 0}, {-3, 0, 1}, {-2, 1, 1}, {-2, 2, 0}, {-2, 0, 0}, {-3, 3, 0}, {-1, -1, 1}, {-2, -1, 0},
    {-1, -2, 0}, {2, -2, 0}, {-3, 1, 1}, {-1, 2, 0}, {0, -3, 1}, {0, 0, 1}, {1, -1, 0}, {0, -1, 1},
    {-1, 0, 1}, {2, 0, 1}, {0, 1, 0}, {0, 2, 1}, {-2, 1, 0}, {-1, 1, 1}, {-3, 0, 0}, {-3, 1, 0},
    {-1, -1, 0}, {1, 1, 1}, {0, -1, 0}, {-3, 2, 1}, {0, -2, 1}, {0, 0, 0}, {-1, 0, 0}, {1, -2, 1},
    {1, 0, 1}, {2, -3, 1}, {2, 0, 0}, {2, -1, 1}, {0, 2, 0}, {-1, 1, 0},
}};
constexpr std::array<std::array<int, 3>, kEdgeCount> kServerEdges = {{
    {-2, 1, 5}, {-2, -1, 1}, {-2, 2, 1}, {-1, -2, 1}, {-1, -1, 5}, {-2, 0, 1}, {1, 1, 0}, {-3, 1, 5},
    {2, -2, 1}, {-3, 2, 0}, {-1, 2, 1}, {1, 0, 0}, {0, -1, 5}, {1, -3, 1}, {0, -2, 0}, {1, -1, 1},
    {0, 0, 5}, {-1, 0, 5}, {0, 1, 1}, {1, -2, 0}, {2, -1, 0}, {0, 2, 5}, {2, 0, 5}, {-2, 1, 1},
    {-1, 1, 5}, {-2, 2, 0}, {-2, 0, 0}, {-1, -1, 1}, {-2, -1, 0}, {-1, -2, 0}, {2, -2, 0}, {1, 1, 5},
    {-1, 2, 0}, {-2, 3, 5}, {0, -3, 1}, {-1, 3, 5}, {0, 0, 1}, {-3, 2, 5}, {0, -1, 1}, {1, -1, 0},
    {1, -2, 5}, {1, 0, 5}, {-1, 0, 1}, {2, 0, 1}, {0, -2, 5}, {0, 1, 0}, {0, 2, 1}, {2, -1, 5},
    {-2, 1, 0}, {-1, 1, 1}, {-3, 0, 0}, {-2, 2, 5}, {-3, 1, 0}, {-2, 0, 5}, {-3, 3, 5}, {-1, -1, 0},
    {1, 1, 1}, {2, -2, 5}, {-1, 2, 5}, {0, -1, 0}, {0, -2, 1}, {0, 0, 0}, {-1, 0, 0}, {1, -2, 1},
    {1, -1, 5}, {1, 0, 1}, {0, 1, 5}, {2, -3, 1}, {2, 0, 0}, {2, -1, 1}, {0, 2, 0}, {-1, 1, 0},
}};

template <std::size_t N, typename Lookup>
constexpr std::array<std::int8_t, N> to_indices(const std::array<std::array<int, 3>, N>& spellings, Lookup lookup) {
  std::array<std::int8_t, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = static_cast<std::int8_t>(lookup(spellings[i][0], spellings[i][1], spellings[i][2]));
  }
  return out;
}

constexpr auto kServerNodeOrder = to_indices(kServerNodes, node_at);
constexpr auto kServerEdgeOrder = to_indices(kServerEdges, edge_at);

template <std::size_t N>
constexpr bool is_permutation_of_indices(const std::array<std::int8_t, N>& order) {
  std::array<bool, N> seen{};
  for (auto i : order) {
    if (i < 0 || static_cast<std::size_t>(i) >= N || seen[static_cast<std::size_t>(i)]) return false;
    seen[static_cast<std::size_t>(i)] = true;
  }
  return true;
}
static_assert(is_permutation_of_indices(kServerNodeOrder), "every corner once");
static_assert(is_permutation_of_indices(kServerEdgeOrder), "every side once");

// Both walk the server's order and stop once every member is out: most
// masks hold a few spots, found long before the end of the order.
NodeList in_server_order(NodeMask mask) {
  NodeList out;
  const int members = count(mask);
  for (auto n : kServerNodeOrder) {
    if (out.size == members) break;
    if (contains(mask, n)) out.push(n);
  }
  return out;
}

EdgeList in_server_order(const EdgeMask& mask) {
  EdgeList out;
  const int members = mask.count();
  for (auto e : kServerEdgeOrder) {
    if (out.size == members) break;
    if (mask.test(e)) out.push(e);
  }
  return out;
}

// The land hexes around a corner by hex index, the order board.go appends
// them in (it walks the tiles in the server's order, which is by index).
struct CornerTiles {
  std::array<std::array<std::int8_t, 3>, kNodeCount> hexes{};
  std::array<std::int8_t, kNodeCount> count{};
  constexpr CornerTiles() {
    for (int h = 0; h < kHexCount; ++h) {
      for (int d = 0; d < kDirections; ++d) {
        const int n = kTopology.hex_nodes[at(h)][at(d)];
        hexes[at(n)][at(count[at(n)]++)] = static_cast<std::int8_t>(h);
      }
    }
  }
};
constexpr CornerTiles kCornerTiles;

// --- What one seat sees: board.go, strategy.go helpers ---

// One decision of one seat, like `player` and `plan` in the Go code.
class Plan {
 public:
  Plan(const Game& g, int seat, const ZooBot& bot, const TableMemory& memory)
      : g_(g), seat_(seat), w_(bot.weights), t_(bot.trading), memory_(memory),
        hand_(g.players[at(seat)].resources) {
    for (auto& o : owner_) o = -1;
    for (int s = 0; s < g.player_count; ++s) {
      const Player& p = g.players[at(s)];
      for_each_node(p.buildings(), [&](int n) { owner_[at(n)] = static_cast<std::int8_t>(s); });
      any_road_ |= p.roads;
    }
    // Game::trade_rate() for all five resources in one pass over the ports
    const NodeMask mine = g.players[at(seat)].buildings();
    bool any_port = false;
    std::array<bool, kResourceCount> two_for_one{};
    for (int i = 0; i < g.board.port_count; ++i) {
      const Port& port = g.board.ports[at(i)];
      for_each_node(port.nodes, [&](int n) { port_[at(n)] = static_cast<int>(port.type); });
      if ((port.nodes & mine) == 0) continue;
      if (port.type == PortType::kAny) {
        any_port = true;
      } else {
        two_for_one[at(static_cast<int>(port.type) - 1)] = true;  // PortType::kWood is 1
      }
    }
    for (int r = 0; r < kResourceCount; ++r) rate_[at(r)] = two_for_one[at(r)] ? 2 : any_port ? 3 : 4;
  }

  // plan(): the part every hard decision works out first.
  void prepare() {
    production_ = produces(seat_);
    compute_scarcity();
    find_target();
    choose_goal();
  }

  std::optional<Action> decide();

 private:
  const Game& g_;
  int seat_;
  const ZooWeights& w_;
  const ZooTrading& t_;
  const TableMemory& memory_;
  const Cards& hand_;

  std::array<std::int8_t, kNodeCount> owner_{};
  std::array<int, kNodeCount> port_ = filled(-1);
  EdgeMask any_road_{};
  std::array<int, kResourceCount> rate_{};

  std::array<double, kResourceCount> production_{};
  std::array<double, kResourceCount> scarcity_{};
  int target_ = -1;
  int target_dist_ = 0;
  enum class Goal : std::uint8_t { kDevCard, kCity, kSettlement };
  Goal goal_ = Goal::kDevCard;
  Cards goal_cost_{};

  static std::array<int, kNodeCount> filled(int v) {
    std::array<int, kNodeCount> a{};
    a.fill(v);
    return a;
  }

  [[nodiscard]] const Player& me() const { return g_.players[at(seat_)]; }
  [[nodiscard]] int points(int seat) const { return g_.visible_points(seat); }
  [[nodiscard]] int victory_points() const { return g_.settings.victory_points > 0 ? g_.settings.victory_points : 10; }

  // free: a corner where a settlement may stand by the distance rule.
  [[nodiscard]] bool free(int n) const {
    if (owner_[at(n)] >= 0) return false;
    for (auto near : kTopology.node_neighbors[at(n)]) {
      if (near >= 0 && owner_[at(near)] >= 0) return false;
    }
    return true;
  }

  // blocked: an opponent builds here, so the bot's roads cannot pass.
  [[nodiscard]] bool blocked(int n) const { return owner_[at(n)] >= 0 && owner_[at(n)] != seat_; }

  // network: every corner the bot can build a road from.
  // Its buildings, and the ends of its roads where no opponent has built.
  [[nodiscard]] NodeMask network() const {
    NodeMask ends = 0;
    for_each_edge(me().roads, [&](int e) {
      ends |= bit(kTopology.edge_nodes[at(e)][0]) | bit(kTopology.edge_nodes[at(e)][1]);
    });
    NodeMask opponents = 0;
    for (int s = 0; s < g_.player_count; ++s) {
      if (s != seat_) opponents |= g_.players[at(s)].buildings();
    }
    return me().buildings() | (ends & ~opponents);
  }

  // distances: new roads needed to reach each corner from the start, through
  // edges nobody has built on and around opponents' buildings; -1 for a
  // corner out of reach.
  [[nodiscard]] std::array<std::int8_t, kNodeCount> distances(NodeMask start, int limit) const {
    std::array<std::int8_t, kNodeCount> dist{};
    dist.fill(-1);
    std::array<std::int8_t, kNodeCount> queue{};
    int head = 0;
    int tail = 0;
    for_each_node(start, [&](int n) {
      dist[at(n)] = 0;
      queue[at(tail++)] = static_cast<std::int8_t>(n);
    });
    while (head < tail) {
      const int n = queue[at(head++)];
      if (dist[at(n)] >= limit || (blocked(n) && dist[at(n)] > 0)) continue;
      for (int k = 0; k < 3; ++k) {
        const int e = kTopology.node_edges[at(n)][at(k)];
        if (e < 0) continue;
        const int near = kTopology.node_neighbors[at(n)][at(k)];
        if (dist[at(near)] >= 0 || any_road_.test(e)) continue;
        dist[at(near)] = static_cast<std::int8_t>(dist[at(n)] + 1);
        queue[at(tail++)] = static_cast<std::int8_t>(near);
      }
    }
    return dist;
  }

  // longestRoad: the longest trail along the bot's roads, with `extra`
  // counted as its own; opponents' buildings break a trail.
  [[nodiscard]] int longest_road(int extra) const {
    EdgeMask own = me().roads;
    if (extra >= 0) own.set(extra);
    EdgeMask used{};
    int longest = 0;
    for (int n = 0; n < kNodeCount; ++n) {
      if ((own & kTopology.node_edge_mask[at(n)]).any()) longest = std::max(longest, walk(n, own, used));
    }
    return longest;
  }

  int walk(int n, const EdgeMask& own, EdgeMask& used) const {
    int best = 0;
    for (int k = 0; k < 3; ++k) {
      const int e = kTopology.node_edges[at(n)][at(k)];
      if (e < 0 || !own.test(e) || used.test(e)) continue;
      used.set(e);
      int length = 1;
      const int near = kTopology.node_neighbors[at(n)][at(k)];
      if (!blocked(near)) length += walk(near, own, used);
      used.reset(e);
      best = std::max(best, length);
    }
    return best;
  }

  // produces: the pips of each resource a player collects, cities twice,
  // without the robbed tile.
  [[nodiscard]] std::array<double, kResourceCount> produces(int seat) const {
    // Go walks the sorted corners; the sums are whole numbers of pips, so the
    // order of adding them does not change the result.
    std::array<double, kResourceCount> production{};
    for_each_node(g_.players[at(seat)].buildings(), [&](int n) {
      const double weight = contains(g_.players[at(seat)].cities, n) ? 2.0 : 1.0;
      for (int i = 0; i < kCornerTiles.count[at(n)]; ++i) {
        const int h = kCornerTiles.hexes[at(n)][at(i)];
        if (g_.board.number[at(h)] != 0 && h != g_.robber_hex) {
          production[at(static_cast<int>(g_.board.terrain[at(h)]))] += weight * pips(g_.board.number[at(h)]);
        }
      }
    });
    return production;
  }

  // scarcity: average pips of a resource over its own pips, so a resource
  // rarely rolled counts for more.
  void compute_scarcity() {
    std::array<double, kResourceCount> totals{};
    double all = 0;
    for (int h = 0; h < kHexCount; ++h) {
      if (g_.board.number[at(h)] == 0) continue;
      totals[at(static_cast<int>(g_.board.terrain[at(h)]))] += pips(g_.board.number[at(h)]);
      all += pips(g_.board.number[at(h)]);
    }
    for (int r = 0; r < kResourceCount; ++r) {
      scarcity_[at(r)] = 1;
      if (totals[at(r)] > 0) {
        scarcity_[at(r)] = std::min(1.6, std::max(0.6, all / static_cast<double>(kResourceCount) / totals[at(r)]));
      }
    }
  }

  // spotValue: rare resources, ones the bot does not collect yet and a port
  // it would use count for more.
  [[nodiscard]] double spot_value(int n) const {
    double value = 0;
    std::array<double, kResourceCount> here{};
    for (int i = 0; i < kCornerTiles.count[at(n)]; ++i) {
      const int h = kCornerTiles.hexes[at(n)][at(i)];
      if (g_.board.number[at(h)] == 0) continue;
      const auto r = at(static_cast<int>(g_.board.terrain[at(h)]));
      const double pip = pips(g_.board.number[at(h)]);
      const double have = production_[r];
      value += pip * scarcity_[r] * w_.resource[r] * (1 - std::min(0.4, have / 25) * w_.variety);
      if (have == 0 && here[r] == 0) value += 2 * w_.variety;
      here[r] += pip;
    }
    const int port = port_[at(n)];
    if (port == static_cast<int>(PortType::kAny)) {
      if (*std::max_element(rate_.begin(), rate_.end()) > 3) value += 1.5 * w_.port;
    } else if (port > 0) {
      const auto r = at(port - 1);  // PortType::kWood is 1, Resource::kWood 0
      if (rate_[r] > 2) value += 0.25 * w_.port * (production_[r] + here[r]);
    }
    return value;
  }

  [[nodiscard]] int settlements() const { return count(me().settlements); }
  [[nodiscard]] int cities() const { return count(me().cities); }

  // findTarget: the free corner worth most for the roads it still needs.
  void find_target() {
    if (settlements() >= kMaxSettlements) return;
    const int roads_left = kMaxRoads - me().roads.count();
    const auto dist = distances(network(), kTargetReach);
    double best = 0;
    for (auto n : corner_order().nodes) {
      const int d = dist[at(n)];
      if (d < 0 || !free(n) || d > roads_left) continue;
      const double value = spot_value(n) / (1 + 0.5 * d);
      if (target_ < 0 || value > best) {
        target_ = n;
        target_dist_ = d;
        best = value;
      }
    }
  }

  // turnsFor: rounds to collect the cost, counting what the bank would give
  // for the cards the cost leaves over.
  [[nodiscard]] double turns_for(const Cards& cost) const {
    int trades = 0;
    for (int r = 0; r < kResourceCount; ++r) {
      if (rate_[at(r)] > 0 && hand_[at(r)] > cost[at(r)]) trades += (hand_[at(r)] - cost[at(r)]) / rate_[at(r)];
    }
    // Go lists one wait per missing card, sorts them and adds up all but the
    // `trades` longest. The waits of one resource are equal, so sorting the
    // five resources by wait and taking whole runs adds the same numbers in
    // the same order.
    std::array<double, kResourceCount> wait{};
    std::array<int, kResourceCount> order{};
    int needed = 0;
    int kinds = 0;
    for (int r = 0; r < kResourceCount; ++r) {
      const int short_by = cost[at(r)] - hand_[at(r)];
      if (short_by <= 0) continue;
      const double per_round = static_cast<double>(g_.player_count) * production_[at(r)] / 36;
      wait[at(r)] = 1 / (per_round + 0.15);
      needed += short_by;
      int i = kinds++;
      for (; i > 0 && wait[at(order[at(i - 1)])] > wait[at(r)]; --i) order[at(i)] = order[at(i - 1)];
      order[at(i)] = r;
    }
    int left = needed - trades;
    double turns = 0;
    for (int i = 0; i < kinds && left > 0; ++i) {
      const int r = order[at(i)];
      const int take = std::min(left, cost[at(r)] - hand_[at(r)]);
      for (int k = 0; k < take; ++k) turns += wait[at(r)];
      left -= take;
    }
    return turns;
  }

  void choose_goal() {
    goal_ = Goal::kDevCard;
    goal_cost_ = kDevCardCost;
    double best_score = kDevCardValue * w_.dev_card / (1 + turns_for(kDevCardCost));
    const auto consider = [&](Goal goal, const Cards& cost, double value) {
      if (const double score = value / (1 + turns_for(cost)); score > best_score) {
        goal_ = goal;
        goal_cost_ = cost;
        best_score = score;
      }
    };
    if (settlements() > 0 && cities() < kMaxCities) {
      double top = 0;
      for (auto n : corner_order().nodes) {
        if (contains(me().settlements, n)) top = std::max(top, spot_value(n));
      }
      consider(Goal::kCity, kCityCost, (1 + top / 10) * w_.city);
    }
    if (target_ >= 0) {
      Cards cost = kSettlementCost;
      for (int r = 0; r < kResourceCount; ++r) cost[at(r)] += kRoadCost[at(r)] * target_dist_;
      consider(Goal::kSettlement, cost, (1 + spot_value(target_) / 10) * w_.settlement);
    }
  }

  // spares: buying this leaves the goal no further away.
  [[nodiscard]] bool spares(const Cards& cost) const {
    return missing(goal_cost_, pay(hand_, cost)) <= missing(goal_cost_, hand_);
  }

  // best: the most valuable corner among the options, the first on a tie.
  [[nodiscard]] int best(const NodeList& options) const {
    int best = options[0];
    double best_value = -1;
    for (auto n : options) {
      if (const double value = spot_value(n); value > best_value) {
        best = n;
        best_value = value;
      }
    }
    return best;
  }

  // roadToward: the road that brings the target one step closer, or -1.
  [[nodiscard]] int road_toward(const EdgeList& options) const {
    if (target_ < 0 || target_dist_ == 0) return -1;
    const auto back = distances(bit(target_), kTargetReach + 1);
    const NodeMask net = network();
    int best = -1;
    int best_dist = target_dist_;
    for (auto e : options) {
      for (auto end : kTopology.edge_nodes[at(e)]) {
        const int d = back[at(end)];
        if (d >= 0 && !contains(net, end) && d < best_dist) {
          best = e;
          best_dist = d;
        }
      }
    }
    return best;
  }

  // longestRoadEdge: a road that brings the bot within reach of the Longest
  // Road, if it is going for it and the road is worth its cards; -1 if none.
  [[nodiscard]] int longest_road_edge(const EdgeList& options) const {
    const int mine = longest_road(-1);
    int holder = 4;
    if (g_.longest_road >= 0) {
      if (g_.longest_road == seat_) return -1;
      holder = std::max(holder, g_.road_lengths[at(g_.longest_road)]);
    }
    if (mine + w_.road_reach <= holder) return -1;
    int best = -1;
    int best_length = mine;
    for (auto e : options) {
      if (const int length = longest_road(e); length > best_length) {
        best = e;
        best_length = length;
      }
    }
    if (best_length == mine) return -1;
    // A road that takes the card is always worth it; one on the way only from spare cards
    return best_length > holder || spares(kRoadCost) ? best : -1;
  }

  // lookAhead: the road leading to the best free corner within two more roads.
  [[nodiscard]] int look_ahead(const EdgeList& options) const {
    int best = options[0];
    double best_value = -1;
    for (auto e : options) {
      for (auto end : kTopology.edge_nodes[at(e)]) {
        const auto dist = distances(bit(end), 2);
        for (int n = 0; n < kNodeCount; ++n) {
          if (dist[at(n)] < 0 || !free(n)) continue;
          if (const double value = spot_value(n) / (1 + 0.5 * (dist[at(n)] + 1)); value > best_value) {
            best = e;
            best_value = value;
          }
        }
      }
    }
    return best;
  }

  // tileHarm: positive if the robber here hurts opponents more than the bot.
  [[nodiscard]] int tile_harm(int hex) const {
    const int p = pips(g_.board.number[at(hex)]);
    int harm = 0;
    for_each_node(kTopology.hex_node_mask[at(hex)], [&](int n) {
      const int o = owner_[at(n)];
      if (o < 0) return;
      int weight = contains(g_.players[at(o)].cities, n) ? 2 : 1;
      if (o == seat_) weight *= -2;
      harm += weight * (p + 1);
    });
    return harm;
  }

  [[nodiscard]] bool robber_on_my_tile() const { return g_.robber_hex >= 0 && tile_harm(g_.robber_hex) < 0; }

  // armyWithinReach: one more knight gives the bot the Largest Army.
  [[nodiscard]] bool army_within_reach() const {
    if (g_.largest_army == seat_) return false;
    int needed = 3;
    if (g_.largest_army >= 0) needed = g_.players[at(g_.largest_army)].knights + 1;
    return me().knights + 1 >= needed;
  }

  // monopoly: the resource that takes most, and how many the others probably
  // hold: whatever is neither in the bank nor in the bot's hand.
  [[nodiscard]] std::pair<Resource, int> monopoly() const {
    int best = 0;
    int best_worth = -1;
    int taken = 0;
    for (int r = 0; r < kResourceCount; ++r) {
      const int held = std::max(0, kCardsOfEach - g_.bank_count(res(r)) - hand_[at(r)]);
      int worth = held;
      if (hand_[at(r)] < goal_cost_[at(r)]) worth *= 2;
      if (worth > best_worth) {
        best = r;
        best_worth = worth;
        taken = held;
      }
    }
    return {res(best), taken};
  }

  // leads: more points than everyone else.
  [[nodiscard]] bool leads(int seat) const {
    for (int s = 0; s < g_.player_count; ++s) {
      if (s != seat && points(s) >= points(seat)) return false;
    }
    return true;
  }

  // embargoed: a wary bot keeps its cards from the leader, anyone close to
  // winning and anyone ahead of the bot itself.
  [[nodiscard]] bool embargoed(int seat) const {
    return leads(seat) || points(seat) + 2 >= victory_points() || points(seat) > points(seat_);
  }

  // threat: an opponent by points, most of all the leader and anyone close
  // to winning.
  [[nodiscard]] int threat(int seat) const {
    const int p = points(seat);
    int lead = 0;
    for (int s = 0; s < g_.player_count; ++s) {
      if (s != seat_) lead = std::max(lead, points(s));
    }
    int weight = 1 + p;
    if (p == lead) weight += 2;
    if (p + 3 >= victory_points()) weight += 4;
    return weight;
  }

  // legal_cities() of game.py walks its dict of buildings, which keeps the
  // order they were built in; spots the memory does not know come after.
  [[nodiscard]] NodeList cities_in_server_order(NodeMask mask) const {
    NodeList out;
    for (int i = 0; i < memory_.built_count; ++i) {
      const int n = memory_.built[at(i)];
      if (contains(mask, n)) {
        out.push(n);
        mask &= ~bit(n);
      }
    }
    for (const auto n : in_server_order(mask)) out.push(n);
    return out;
  }

  std::optional<Action> hard_discard();
  std::optional<Action> hard_answer();
  std::optional<Action> hard_setup();
  std::optional<Action> hard_road();
  std::optional<Action> hard_main();
  std::optional<Action> follow_offer();
  std::optional<Action> play_card(const EdgeList& roads);
  std::optional<Action> trade();
  std::optional<Action> shed(const EdgeList& roads);
  int hard_robber() const;
  int robber_hex_medium(std::uint32_t options) const;
  int hard_victim() const;
};

Action build(ActionKind kind, int target = -1) {
  Action a;
  a.kind = kind;
  a.target = target;
  return a;
}

// hardDiscard: gives up what the goal does not need first.
std::optional<Action> Plan::hard_discard() {
  prepare();
  Cards hand = hand_;
  Action a = build(ActionKind::kDiscard);
  for (int k = 0; k < g_.pending_discards[at(seat_)]; ++k) {
    int largest = -1;
    for (int r = 0; r < kResourceCount; ++r) {
      if (hand[at(r)] == 0) continue;
      const int extra = hand[at(r)] - goal_cost_[at(r)];
      if (largest < 0 || extra > hand[at(largest)] - goal_cost_[at(largest)]) largest = r;
    }
    --hand[at(largest)];
    ++a.give[at(largest)];
  }
  return a;
}

// hardAnswer: takes an offer that brings its goal closer, unless it would
// help the leader or someone about to win.
std::optional<Action> Plan::hard_answer() {
  const TradeOffer& offer = g_.offer;
  if (!offer.open || offer.proposer == seat_ || ((offer.accepted | offer.declined) >> seat_ & 1U) != 0) {
    return std::nullopt;
  }
  bool accept = t_.accepts && missing(offer.get, hand_) == 0;
  const int proposer = offer.proposer;
  if (t_.embargo >= 2 && embargoed(proposer)) {
    accept = false;
  } else if (t_.embargo >= 1 && (leads(proposer) || points(proposer) + 2 >= victory_points())) {
    accept = false;
  }
  if (accept) {
    prepare();
    Cards after = pay(hand_, offer.get);
    for (int r = 0; r < kResourceCount; ++r) after[at(r)] += offer.give[at(r)];
    const int before = missing(goal_cost_, hand_);
    const int then = missing(goal_cost_, after);
    accept = then < before || (t_.neutral && then == before);
  }
  Action a = build(ActionKind::kRespondTrade);
  a.accept = accept;
  return a;
}

std::optional<Action> Plan::hard_setup() {
  prepare();
  const NodeList spots = in_server_order(g_.legal_settlements(seat_));
  if (!spots.empty()) return build(ActionKind::kBuildSettlement, best(spots));
  const EdgeList roads = in_server_order(g_.legal_roads(seat_));
  if (roads.empty()) return std::nullopt;
  return build(ActionKind::kBuildRoad, look_ahead(roads));
}

std::optional<Action> Plan::hard_road() {
  prepare();
  const EdgeList roads = in_server_order(g_.legal_roads(seat_));
  if (roads.empty()) return std::nullopt;
  if (const int e = road_toward(roads); e >= 0) return build(ActionKind::kBuildRoad, e);
  if (const int e = longest_road_edge(roads); e >= 0) return build(ActionKind::kBuildRoad, e);
  return build(ActionKind::kBuildRoad, look_ahead(roads));
}

// followOffer: waits for every answer, then confirms the first player to
// accept or withdraws the offer.
std::optional<Action> Plan::follow_offer() {
  const TradeOffer& offer = g_.offer;
  if (std::popcount(static_cast<unsigned>(offer.accepted | offer.declined)) < g_.player_count - 1) {
    return std::nullopt;
  }
  if (missing(offer.give, hand_) == 0) {
    // In the order they accepted; seats the memory does not know come last.
    std::array<int, kMaxPlayers> order{};
    int n = 0;
    unsigned left = offer.accepted;
    for (int i = 0; i < memory_.accepted_count; ++i) {
      const int s = memory_.accepted[at(i)];
      if ((left >> s & 1U) != 0) {
        order[at(n++)] = s;
        left &= ~(1U << s);
      }
    }
    for (int s = 0; s < kMaxPlayers; ++s) {
      if ((left >> s & 1U) != 0) order[at(n++)] = s;
    }
    for (int i = 0; i < n; ++i) {
      if (t_.embargo < 2 || !embargoed(order[at(i)])) return build(ActionKind::kConfirmTrade, order[at(i)]);
    }
  }
  return build(ActionKind::kCancelTrade);
}

std::optional<Action> Plan::play_card(const EdgeList& roads) {
  const std::uint8_t playable = g_.playable_dev_cards(seat_);
  if (playable == 0) return std::nullopt;
  const auto has = [&](DevCard c) { return (playable >> static_cast<int>(c) & 1U) != 0; };
  const bool knight = has(DevCard::kKnight);
  if (knight && (robber_on_my_tile() || army_within_reach())) return build(ActionKind::kPlayKnight);
  if (has(DevCard::kMonopoly)) {
    if (const auto [r, taken] = monopoly(); taken >= 3) {
      Action a = build(ActionKind::kPlayMonopoly);
      a.first = r;
      return a;
    }
  }
  if (has(DevCard::kYearOfPlenty)) {
    std::array<int, 2> wanted{};
    int n = 0;
    Cards bank{};
    for (int r = 0; r < kResourceCount; ++r) bank[at(r)] = g_.bank_count(res(r));
    for (int r = 0; r < kResourceCount; ++r) {
      for (int k = 0; k < std::max(0, goal_cost_[at(r)] - hand_[at(r)]); ++k) {
        if (bank[at(r)] > 0 && n < 2) {
          --bank[at(r)];
          wanted[at(n++)] = r;
        }
      }
    }
    if (n == 1 && bank[at(wanted[0])] > 0) wanted[at(n++)] = wanted[0];
    if (n == 2) {
      Action a = build(ActionKind::kPlayYearOfPlenty);
      a.first = res(wanted[0]);
      a.second = res(wanted[1]);
      return a;
    }
  }
  if (has(DevCard::kRoadBuilding) && !roads.empty()) {
    if (road_toward(roads) >= 0 || longest_road_edge(roads) >= 0) return build(ActionKind::kPlayRoadBuilding);
  }
  if (knight) return build(ActionKind::kPlayKnight);
  return std::nullopt;
}

// trade: asks the players, then the bank, for a card the goal still needs,
// giving only cards the goal leaves over.
std::optional<Action> Plan::trade() {
  const Cards& cost = goal_cost_;
  std::array<int, kResourceCount> wanted{};
  int wanted_count = 0;
  for (int r = 0; r < kResourceCount; ++r) {
    if (hand_[at(r)] < cost[at(r)]) wanted[at(wanted_count++)] = r;
  }
  if (wanted_count == 0) return std::nullopt;
  // The resource the bot rolls least is the hardest to get on its own. Go
  // compares int(a - b); production is a whole number of pips, so this is a < b.
  std::stable_sort(wanted.begin(), wanted.begin() + wanted_count, [&](int a, int b) {
    return static_cast<int>(production_[at(a)] - production_[at(b)]) < 0;
  });
  std::array<int, kResourceCount> spare{0, 1, 2, 3, 4};
  std::stable_sort(spare.begin(), spare.end(), [&](int a, int b) {
    return (hand_[at(b)] - cost[at(b)]) - (hand_[at(a)] - cost[at(a)]) < 0;
  });

  // offers: true for the hard level and every zoo bot (strategy.go)
  int offer_count = 0;
  int chosen_give = -1;
  int chosen_want = -1;
  for (int i = 0; i < wanted_count; ++i) {
    const int want = wanted[at(i)];
    // Only worth asking when the others hold some
    if (kCardsOfEach - g_.bank_count(res(want)) - hand_[at(want)] < 2) continue;
    for (const int give : spare) {
      if (hand_[at(give)] - cost[at(give)] >= 1 && rate_[at(give)] > 2) {
        if (offer_count == memory_.offers_made) {
          chosen_give = give;
          chosen_want = want;
        }
        ++offer_count;
      }
    }
  }
  if (const int made = memory_.offers_made; made < t_.offers_per_turn && made < offer_count) {
    Action a = build(ActionKind::kOfferTrade);
    a.give[at(chosen_give)] = 1;
    a.get[at(chosen_want)] = 1;
    return a;
  }
  for (int i = 0; i < wanted_count; ++i) {
    const int want = wanted[at(i)];
    if (g_.bank_count(res(want)) == 0) continue;
    for (const int give : spare) {
      if (rate_[at(give)] > 0 && hand_[at(give)] - cost[at(give)] >= rate_[at(give)]) {
        Action a = build(ActionKind::kTradeWithBank);
        a.first = res(give);
        a.second = res(want);
        return a;
      }
    }
  }
  return std::nullopt;
}

// shed: spends cards before ending a turn with more than a 7 would leave.
std::optional<Action> Plan::shed(const EdgeList& roads) {
  if (g_.can_buy_dev_card(seat_)) return build(ActionKind::kBuyDevCard);
  if (!roads.empty()) {
    if (const int e = road_toward(roads); e >= 0) return build(ActionKind::kBuildRoad, e);
    return build(ActionKind::kBuildRoad, look_ahead(roads));
  }
  return std::nullopt;
}

std::optional<Action> Plan::hard_main() {
  if (g_.offer.open && g_.offer.proposer == seat_) return follow_offer();
  prepare();
  if (const NodeList c = cities_in_server_order(g_.legal_cities(seat_)); !c.empty()) {
    return build(ActionKind::kUpgradeCity, best(c));
  }
  if (const NodeList s = in_server_order(g_.legal_settlements(seat_)); !s.empty()) {
    return build(ActionKind::kBuildSettlement, best(s));
  }
  const EdgeList roads = in_server_order(g_.legal_roads(seat_));
  if (auto card = play_card(roads)) return card;
  if (!roads.empty()) {
    if (const int e = road_toward(roads); e >= 0 && (goal_ == Goal::kSettlement || spares(kRoadCost))) {
      return build(ActionKind::kBuildRoad, e);
    }
    if (const int e = longest_road_edge(roads); e >= 0) return build(ActionKind::kBuildRoad, e);
  }
  if (g_.can_buy_dev_card(seat_) && (goal_ == Goal::kDevCard || spares(kDevCardCost))) {
    return build(ActionKind::kBuyDevCard);
  }
  if (auto t = trade()) return t;
  const int limit = g_.settings.discard_limit > 0 ? g_.settings.discard_limit : 7;
  if (total(hand_) > limit) {
    if (auto spend = shed(roads)) return spend;
  }
  return build(ActionKind::kEndTurn);
}

// robberHex (medium): the tile that hurts opponents most, the first on a tie.
int Plan::robber_hex_medium(std::uint32_t options) const {
  int best = -1;
  int best_harm = 0;
  for (int h = 0; h < kHexCount; ++h) {
    if ((options >> h & 1U) == 0) continue;
    if (const int harm = tile_harm(h); best < 0 || harm > best_harm) {
      best = h;
      best_harm = harm;
    }
  }
  return best;
}

// hardRobber: blocks the tile that costs the leaders most, never one of its own.
int Plan::hard_robber() const {
  const std::uint32_t options = g_.legal_robber_hexes(seat_);
  int best = -1;
  int best_harm = 0;
  for (int h = 0; h < kHexCount; ++h) {
    if ((options >> h & 1U) == 0) continue;
    int harm = 0;
    bool own = false;
    bool victims = false;
    for_each_node(kTopology.hex_node_mask[at(h)], [&](int n) {
      const int o = owner_[at(n)];
      if (o < 0 || own) return;
      if (o == seat_) {
        own = true;
        return;
      }
      int weight = threat(o) * (pips(g_.board.number[at(h)]) + 1);
      if (contains(g_.players[at(o)].cities, n)) weight *= 2;
      harm += weight;
      victims = victims || g_.players[at(o)].card_count() > 0;
    });
    if (own) continue;
    if (victims) harm += 3;
    if (best < 0 || harm > best_harm) {
      best = h;
      best_harm = harm;
    }
  }
  return best >= 0 ? best : robber_hex_medium(options);
}

// hardVictim: robs the leader, the one with more cards on a tie.
int Plan::hard_victim() const {
  const std::uint8_t victims = g_.steal_victims(seat_);
  int best = -1;
  int best_score = 0;
  for (int s = 0; s < g_.player_count; ++s) {
    if ((victims >> s & 1U) == 0) continue;
    // A stable sort by score, highest first, takes the first of the highest.
    if (const int score = 2 * threat(s) + g_.players[at(s)].card_count(); best < 0 || score > best_score) {
      best = s;
      best_score = score;
    }
  }
  return best;
}

std::optional<Action> Plan::decide() {
  if (g_.game_over) return std::nullopt;
  if (g_.pending_discards[at(seat_)] > 0) return hard_discard();
  if (auto answer = hard_answer()) return answer;
  if (g_.current != seat_) return std::nullopt;
  switch (g_.phase) {
    case Phase::kSetup:
      return hard_setup();
    case Phase::kRollDice:
      // defensiveKnight: a knight before rolling frees its own tile
      if (robber_on_my_tile() && (g_.playable_dev_cards(seat_) >> static_cast<int>(DevCard::kKnight) & 1U) != 0) {
        return build(ActionKind::kPlayKnight);
      }
      return build(ActionKind::kRollDice);
    case Phase::kMoveRobber:
      return build(ActionKind::kMoveRobber, hard_robber());
    case Phase::kSteal:
      return build(ActionKind::kSteal, hard_victim());
    case Phase::kRoadBuilding:
      return hard_road();
    case Phase::kMain:
      return hard_main();
    case Phase::kDiscard:
      return std::nullopt;
  }
  return std::nullopt;
}

// exp(x) from its Taylor series, for |x| <= ~1.5. std::exp may round
// differently in glibc and in Emscripten's libc; this uses only * and +, so
// the shaken weights are the same bits everywhere.
double portable_exp(double x) {
  double sum = 1;
  double term = 1;
  for (int k = 1; k < 30; ++k) {
    term *= x / k;
    sum += term;
  }
  return sum;
}

// A standard normal draw as the sum of 12 uniforms minus 6 (Irwin-Hall):
// mean 0, variance 1, tails cut at 6 sigma, and no log or cos (see above).
double normal(Rng& rng) {
  double sum = 0;
  for (int i = 0; i < 12; ++i) sum += static_cast<double>(rng.next() >> 11) * 0x1.0p-53;
  return sum - 6;
}

}  // namespace

// --- Public functions ---

std::string_view action_name(ActionKind kind) noexcept {
  switch (kind) {
    case ActionKind::kBuildSettlement: return "build_settlement";
    case ActionKind::kBuildRoad: return "build_road";
    case ActionKind::kUpgradeCity: return "upgrade_city";
    case ActionKind::kRollDice: return "roll_dice";
    case ActionKind::kDiscard: return "discard";
    case ActionKind::kMoveRobber: return "move_robber";
    case ActionKind::kSteal: return "steal";
    case ActionKind::kTradeWithBank: return "trade_with_bank";
    case ActionKind::kOfferTrade: return "offer_trade";
    case ActionKind::kRespondTrade: return "respond_trade";
    case ActionKind::kConfirmTrade: return "confirm_trade";
    case ActionKind::kCancelTrade: return "cancel_trade";
    case ActionKind::kBuyDevCard: return "buy_dev_card";
    case ActionKind::kPlayKnight: return "play_knight";
    case ActionKind::kPlayRoadBuilding: return "play_road_building";
    case ActionKind::kPlayYearOfPlenty: return "play_year_of_plenty";
    case ActionKind::kPlayMonopoly: return "play_monopoly";
    case ActionKind::kEndTurn: return "end_turn";
  }
  return "?";
}

std::string_view archetype_name(Archetype a) noexcept {
  switch (a) {
    case Archetype::kBalanced: return "balanced";
    case Archetype::kOreWheat: return "ore-wheat";
    case Archetype::kWoodBrick: return "wood-brick";
    case Archetype::kPort: return "port";
  }
  return "?";
}

std::string_view personality_name(Personality p) noexcept {
  switch (p) {
    case Personality::kFair: return "fair";
    case Personality::kFriendly: return "friendly";
    case Personality::kWary: return "wary";
    case Personality::kLoner: return "loner";
  }
  return "?";
}

Error apply_action(Game& game, int seat, const Action& a, Rng& rng) {
  switch (a.kind) {
    case ActionKind::kBuildSettlement: return game.build_settlement(seat, a.target);
    case ActionKind::kBuildRoad: return game.build_road(seat, a.target);
    case ActionKind::kUpgradeCity: return game.upgrade_city(seat, a.target);
    case ActionKind::kRollDice: {
      const int d1 = rng.die();
      return game.roll_dice(seat, d1, rng.die());
    }
    case ActionKind::kDiscard: return game.discard(seat, a.give);
    case ActionKind::kMoveRobber: return game.move_robber(seat, a.target);
    case ActionKind::kSteal: {
      if (a.target < 0 || a.target >= game.player_count) return Error::kBadArgument;
      return game.steal(seat, a.target, random_card(game.players[at(a.target)].resources, rng));
    }
    case ActionKind::kTradeWithBank: return game.trade_with_bank(seat, a.first, a.second);
    case ActionKind::kOfferTrade: return game.offer_trade(seat, a.give, a.get);
    case ActionKind::kRespondTrade: return game.respond_trade(seat, a.accept);
    case ActionKind::kConfirmTrade: return game.confirm_trade(seat, a.target);
    case ActionKind::kCancelTrade: return game.cancel_trade(seat);
    case ActionKind::kBuyDevCard: return game.buy_dev_card(seat);
    case ActionKind::kPlayKnight: return game.play_knight(seat);
    case ActionKind::kPlayRoadBuilding: return game.play_road_building(seat);
    case ActionKind::kPlayYearOfPlenty: return game.play_year_of_plenty(seat, a.first, a.second);
    case ActionKind::kPlayMonopoly: return game.play_monopoly(seat, a.first);
    case ActionKind::kEndTurn: return game.end_turn(seat);
  }
  return Error::kBadArgument;
}

ZooWeights archetype_weights(Archetype a) noexcept {
  ZooWeights w;
  switch (a) {
    case Archetype::kBalanced:
      break;
    case Archetype::kOreWheat:  // cities and development cards: ore and wheat, few roads
      w.resource = {0.75, 0.75, 1.1, 1.3, 1.4};
      w.variety = 0.8;
      w.city = 1.3;
      w.settlement = 0.9;
      w.dev_card = 1.6;
      w.road_reach = 1;
      break;
    case Archetype::kWoodBrick:  // spread out: settlements and the Longest Road
      w.resource = {1.4, 1.4, 1, 1, 0.7};
      w.variety = 0.8;
      w.city = 0.85;
      w.settlement = 1.3;
      w.dev_card = 0.6;
      w.road_reach = 3;
      break;
    case Archetype::kPort:  // lots of one resource and the port that sells it
      w.variety = 0.3;
      w.port = 3;
      w.settlement = 1.1;
      w.dev_card = 0.9;
      break;
  }
  return w;
}

ZooTrading personality_trading(Personality p) noexcept {
  switch (p) {
    case Personality::kFair: return {2, true, false, 1};
    case Personality::kFriendly: return {3, true, true, 0};
    case Personality::kWary: return {1, true, false, 2};
    case Personality::kLoner: return {0, false, false, 2};
  }
  return {};
}

ZooBot make_zoo_bot(Archetype a, Personality p, std::uint64_t seed) {
  ZooBot bot{a, p, seed, archetype_weights(a), personality_trading(p)};
  if (seed == 0) return bot;
  // Same weights in the same order as jitter() in zoo.go
  Rng rng(seed);
  const auto shake = [&](double& v) { v *= portable_exp(kJitterSpread * normal(rng)); };
  for (auto& r : bot.weights.resource) shake(r);
  shake(bot.weights.variety);
  shake(bot.weights.port);
  shake(bot.weights.city);
  shake(bot.weights.settlement);
  shake(bot.weights.dev_card);
  return bot;
}

ZooBot hard_bot() { return make_zoo_bot(Archetype::kBalanced, Personality::kFair, 0); }

ZooBot random_zoo_bot(Rng& rng) {
  const auto a = static_cast<Archetype>(rng.below(kArchetypeCount));
  const auto p = static_cast<Personality>(rng.below(kPersonalityCount));
  // Below 2^53 like the platform's seeds, and never 0 (no shaking)
  const std::uint64_t seed = (rng.next() >> 11) | 1U;
  return make_zoo_bot(a, p, seed);
}

void TableMemory::sync(const Game& game) noexcept {
  if (game.turn_number != turn) {
    turn = game.turn_number;
    offers_made = 0;
    accepted_count = 0;
  }
  if (!game.offer.open) accepted_count = 0;
}

void TableMemory::record(int seat, const Action& action) noexcept {
  if (action.kind == ActionKind::kOfferTrade) {
    ++offers_made;
    accepted_count = 0;
  } else if (action.kind == ActionKind::kRespondTrade) {
    // Answering again moves a player to the end, as respond_to_trade() in
    // game.py removes the old answer and appends the new one.
    int kept = 0;
    for (int i = 0; i < accepted_count; ++i) {
      if (accepted[at(i)] != seat) accepted[at(kept++)] = accepted[at(i)];
    }
    accepted_count = kept;
    if (action.accept && accepted_count < kMaxPlayers) accepted[at(accepted_count++)] = static_cast<std::int8_t>(seat);
  } else if (action.kind == ActionKind::kBuildSettlement && built_count < static_cast<int>(built.size())) {
    built[at(built_count++)] = static_cast<std::int8_t>(action.target);
  }
}

std::optional<Action> zoo_decide(const Game& game, int seat, const ZooBot& bot, const TableMemory& memory) {
  if (seat < 0 || seat >= game.player_count) return std::nullopt;
  Plan plan(game, seat, bot, memory);
  return plan.decide();
}

int next_actor(const Game& game) {
  if (game.game_over) return -1;
  for (int s = 0; s < game.player_count; ++s) {
    if (game.pending_discards[at(s)] > 0) return s;
  }
  if (game.offer.open) {
    for (int s = 0; s < game.player_count; ++s) {
      if (s != game.offer.proposer && ((game.offer.accepted | game.offer.declined) >> s & 1U) == 0) return s;
    }
  }
  return game.current;
}

ZooStepResult play_zoo_step(ZooTable& table, Rng& rng) {
  Game& game = table.game;
  const int seat = next_actor(game);
  if (seat < 0) return {Error::kGameOver, false};
  table.memory.sync(game);
  if (auto action = zoo_decide(game, seat, table.bots[at(seat)], table.memory)) {
    if (const Error e = apply_action(game, seat, *action, rng); e == Error::kNone) {
      table.memory.record(seat, *action);
      return {};
    }
  }
  // The fallback of Decide(): withdraw an own offer, or end the turn. Other
  // phases have no safe move in Go (the server would refuse again); a random
  // legal one keeps the simulation going and the step counts as a fallback.
  Action safe = build(ActionKind::kEndTurn);
  if (game.offer.open && game.offer.proposer == seat) safe = build(ActionKind::kCancelTrade);
  if (game.phase == Phase::kMain && seat == game.current && apply_action(game, seat, safe, rng) == Error::kNone) {
    return {Error::kNone, true};
  }
  return {play_random_step(game, rng), true};
}

ZooPlayout play_out_zoo(ZooTable& table, Rng& rng, int max_actions) {
  ZooPlayout out;
  while (!table.game.game_over && out.actions < max_actions) {
    const ZooStepResult step = play_zoo_step(table, rng);
    ++out.actions;
    if (step.fallback) ++out.fallbacks;
  }
  out.winner = table.game.winner;
  return out;
}

const std::array<std::int8_t, kNodeCount>& server_node_order() noexcept { return kServerNodeOrder; }
const std::array<std::int8_t, kEdgeCount>& server_edge_order() noexcept { return kServerEdgeOrder; }

}  // namespace cae
