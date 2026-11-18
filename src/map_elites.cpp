#include "catan/map_elites.hpp"

#include <algorithm>
#include <cmath>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

template <std::size_t N>
int bin(const std::array<double, N>& edges, double v) noexcept {
  int b = 0;
  while (b < static_cast<int>(N) && v >= edges[at(b)]) ++b;
  return b;
}

// Standard normal as in zoo.cpp (Irwin-Hall). The search runs natively only,
// so std::exp is fine here: elites travel as their weights, not as a seed.
double normal(Rng& rng) {
  double sum = 0;
  for (int i = 0; i < 12; ++i) sum += static_cast<double>(rng.next() >> 11) * 0x1.0p-53;
  return sum - 6;
}

}  // namespace

CellBins bins_of(const Behavior& b) noexcept {
  return {bin(kCityShareEdges, b.city_share), bin(kTradeEdges, b.trades), bin(kDevCardEdges, b.dev_cards)};
}

int cell_of(const CellBins& b) noexcept { return b.city + kCityBins * (b.trades + kTradeBins * b.dev_cards); }

CellBins bins_of_cell(int cell) noexcept {
  return {cell % kCityBins, (cell / kCityBins) % kTradeBins, cell / (kCityBins * kTradeBins)};
}

Evaluation& Evaluation::operator+=(const Evaluation& o) noexcept {
  games += o.games;
  wins += o.wins;
  unfinished += o.unfinished;
  city_share += o.city_share;
  trades += o.trades;
  dev_cards += o.dev_cards;
  return *this;
}

double Evaluation::margin() const noexcept {
  if (games < 2) return 0.0;
  const double p = win_rate();
  return 1.96 * std::sqrt(p * (1 - p) / static_cast<double>(games));
}

Behavior Evaluation::behavior() const noexcept {
  if (games == 0) return {};
  const auto n = static_cast<double>(games);
  return {city_share / n, trades / n, dev_cards / n};
}

std::array<ZooBot, 3> deal_opponents(std::uint64_t deal_seed, std::span<const ZooBot> pool) {
  std::array<ZooBot, 3> out;
  for (int slot = 0; slot < 3; ++slot) {
    // The same stream as a `zoo` opponent in tools/arena.cpp.
    Rng rng(deal_seed ^ (0x5eed0000ULL + static_cast<std::uint64_t>(slot)));
    out[at(slot)] = pool.empty() ? random_zoo_bot(rng) : pool[at(rng.below(static_cast<int>(pool.size())))];
  }
  return out;
}

Evaluation evaluate_deal(const ZooBot& bot, std::uint64_t seed, int index, std::span<const ZooBot> pool) {
  const Deal deal = make_deal(seed, index);
  const auto opponents = deal_opponents(deal.seed, pool);
  Rng pick(deal.seed);
  const int seat = pick.below(kMaxPlayers);
  std::array<ArenaPlayer, kMaxPlayers> table;
  int next = 0;
  for (int s = 0; s < kMaxPlayers; ++s) table[at(s)] = zoo_player(s == seat ? bot : opponents[at(next++)]);
  const ArenaGame g = play_arena_game(deal, table, 0, false);

  Evaluation e;
  e.games = 1;
  if (g.winner == seat) e.wins = 1;
  if (g.winner < 0) e.unfinished = 1;
  const SeatPlay& p = g.play[at(seat)];
  const int building_points = p.settlements + 2 * p.cities;
  e.city_share = building_points > 0 ? 2.0 * p.cities / building_points : 0.0;
  e.trades = p.trades;
  e.dev_cards = p.dev_cards;
  return e;
}

ZooBot mutate(const ZooBot& parent, Rng& rng) {
  ZooBot c = parent;
  c.seed = 0;
  const auto scale = [&](double& v) {
    if (rng.chance(0.5)) v = std::clamp(v * std::exp(0.25 * normal(rng)), 0.05, 5.0);
  };
  const auto step = [&](int& v, double p, int lo, int hi) {
    if (rng.chance(p)) v = std::clamp(v + (rng.chance(0.5) ? 1 : -1), lo, hi);
  };
  ZooWeights& w = c.weights;
  for (auto& r : w.resource) scale(r);
  scale(w.variety);
  scale(w.port);
  scale(w.city);
  scale(w.settlement);
  scale(w.dev_card);
  step(w.road_reach, 0.2, 0, 4);
  if (rng.chance(0.3)) w.leader_focus = std::clamp(w.leader_focus + 0.5 * normal(rng), 0.0, 4.0);

  ZooTrading& t = c.trading;
  step(t.offers_per_turn, 0.2, 0, 4);
  step(t.embargo, 0.15, 0, 2);
  if (rng.chance(0.05)) t.accepts = !t.accepts;
  if (rng.chance(0.1)) t.neutral = !t.neutral;
  return c;
}

std::uint64_t fresh_seed(std::uint64_t seed, int born) noexcept {
  return (seed + 1000003) * 0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(born) * 0xbf58476d1ce4e5b9ULL + 1;
}

int Archive::filled() const noexcept {
  return static_cast<int>(std::count_if(cells.begin(), cells.end(), [](const auto& c) { return c.has_value(); }));
}

std::vector<int> Archive::filled_cells() const {
  std::vector<int> out;
  for (int i = 0; i < kStyleCells; ++i) {
    if (cells[at(i)]) out.push_back(i);
  }
  return out;
}

double Archive::mean_score() const noexcept {
  double sum = 0;
  int n = 0;
  for (const auto& c : cells) {
    if (c) {
      sum += c->score().win_rate();
      ++n;
    }
  }
  return n > 0 ? sum / n : 0.0;
}

}  // namespace cae
