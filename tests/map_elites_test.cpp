// MAP-Elites: the grid of styles, mutation, evaluation, and elites as JSON.

#include "catan/elites.hpp"
#include "catan/map_elites.hpp"

#include <gtest/gtest.h>

#include <set>

namespace cae {
namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

TEST(MapElites, EveryCellHasItsOwnBins) {
  std::set<int> seen;
  for (int cell = 0; cell < kStyleCells; ++cell) {
    const CellBins b = bins_of_cell(cell);
    EXPECT_EQ(cell_of(b), cell);
    seen.insert(cell);
  }
  EXPECT_EQ(static_cast<int>(seen.size()), kStyleCells);
}

TEST(MapElites, BinsFollowTheEdges) {
  EXPECT_EQ(bins_of({0.0, 0.0, 0.0}).city, 0);
  EXPECT_EQ(bins_of({kCityShareEdges[0], 0, 0}).city, 1);  // an edge opens the next bin
  EXPECT_EQ(bins_of({1.0, 0, 0}).city, kCityBins - 1);
  EXPECT_EQ(bins_of({0, 0.4, 0}).trades, 0);  // under one trade in two games: no trading
  EXPECT_EQ(bins_of({0, 50, 0}).trades, kTradeBins - 1);
  EXPECT_EQ(bins_of({0, 0, 4.0}).dev_cards, 2);
}

TEST(MapElites, MutationIsReproducibleAndStaysInRange) {
  const auto lineage = [](std::uint64_t seed) {
    Rng rng(seed);
    std::vector<ZooBot> out{hard_bot()};
    for (int i = 0; i < 300; ++i) out.push_back(mutate(out.back(), rng));
    return out;
  };
  const auto a = lineage(9);
  const auto b = lineage(9);
  bool changed = false;
  for (std::size_t i = 1; i < a.size(); ++i) {
    const ZooBot& child = a[i];
    ASSERT_EQ(child.weights.resource, b[i].weights.resource);
    ASSERT_EQ(child.weights.leader_focus, b[i].weights.leader_focus);
    ASSERT_EQ(child.trading.embargo, b[i].trading.embargo);
    for (double r : child.weights.resource) {
      EXPECT_GE(r, 0.05);
      EXPECT_LE(r, 5.0);
    }
    EXPECT_GE(child.weights.leader_focus, 0.0);
    EXPECT_LE(child.weights.leader_focus, 4.0);
    EXPECT_GE(child.weights.road_reach, 0);
    EXPECT_LE(child.weights.road_reach, 4);
    EXPECT_GE(child.trading.offers_per_turn, 0);
    EXPECT_LE(child.trading.offers_per_turn, 4);
    EXPECT_GE(child.trading.embargo, 0);
    EXPECT_LE(child.trading.embargo, 2);
    EXPECT_EQ(child.seed, 0U);
    changed = changed || child.weights.city != a[i - 1].weights.city;
  }
  EXPECT_TRUE(changed);
}

TEST(MapElites, AnEvaluationIsOneReproducibleGame) {
  const ZooBot bot = hard_bot();
  int wins = 0;
  for (int i = 0; i < 40; ++i) {
    const Evaluation e = evaluate_deal(bot, 3, i);
    const Evaluation again = evaluate_deal(bot, 3, i);
    EXPECT_EQ(e.games, 1);
    EXPECT_EQ(e.wins, again.wins);
    EXPECT_EQ(e.trades, again.trades);
    EXPECT_EQ(e.dev_cards, again.dev_cards);
    EXPECT_GE(e.city_share, 0.0);
    EXPECT_LE(e.city_share, 1.0);
    wins += static_cast<int>(e.wins);
  }
  EXPECT_GT(wins, 0);   // it is not losing every game
  EXPECT_LT(wins, 40);  // nor winning every one
}

TEST(MapElites, ALonerNeverTrades) {
  const ZooBot loner = make_zoo_bot(Archetype::kBalanced, Personality::kLoner, 0);
  Evaluation sum;
  for (int i = 0; i < 20; ++i) sum += evaluate_deal(loner, 5, i);
  EXPECT_EQ(sum.trades, 0.0);
  EXPECT_EQ(bins_of(sum.behavior()).trades, 0);
}

TEST(MapElites, OpponentsFromAPool) {
  const std::array<ZooBot, 1> pool{make_zoo_bot(Archetype::kPort, Personality::kWary, 0)};
  for (const ZooBot& b : deal_opponents(77, pool)) EXPECT_EQ(b.archetype, Archetype::kPort);
  const auto a = deal_opponents(77);
  const auto b = deal_opponents(77);
  for (int i = 0; i < 3; ++i) EXPECT_EQ(a[at(i)].seed, b[at(i)].seed);
}

TEST(MapElites, TheArchiveKeepsTheFreshScore) {
  Elite e;
  e.search.games = 400;
  e.search.wins = 200;
  EXPECT_DOUBLE_EQ(e.score().win_rate(), 0.5);  // nothing fresh yet: the search score
  e.fresh.games = 800;
  e.fresh.wins = 240;
  EXPECT_DOUBLE_EQ(e.score().win_rate(), 0.3);
  Archive archive;
  archive.cells[5] = e;
  EXPECT_EQ(archive.filled(), 1);
  EXPECT_EQ(archive.filled_cells(), std::vector<int>{5});
  EXPECT_DOUBLE_EQ(archive.mean_score(), 0.3);
  EXPECT_NE(fresh_seed(1, 10), fresh_seed(1, 11));
}

TEST(Elites, BotsSurviveJson) {
  Rng rng(4);
  ZooBot bot = make_zoo_bot(Archetype::kOreWheat, Personality::kWary, 99);
  for (int i = 0; i < 20; ++i) bot = mutate(bot, rng);
  const ZooBot back = zoo_bot_from_json(nlohmann::json::parse(zoo_bot_json(bot).dump()));
  EXPECT_EQ(back.archetype, bot.archetype);
  EXPECT_EQ(back.personality, bot.personality);
  EXPECT_EQ(back.weights.resource, bot.weights.resource);
  EXPECT_EQ(back.weights.city, bot.weights.city);
  EXPECT_EQ(back.weights.leader_focus, bot.weights.leader_focus);
  EXPECT_EQ(back.weights.road_reach, bot.weights.road_reach);
  EXPECT_EQ(back.trading.offers_per_turn, bot.trading.offers_per_turn);
  EXPECT_EQ(back.trading.neutral, bot.trading.neutral);
  EXPECT_EQ(back.trading.embargo, bot.trading.embargo);
  // The same bot plays the same game
  EXPECT_EQ(evaluate_deal(back, 2, 7).wins, evaluate_deal(bot, 2, 7).wins);
  EXPECT_EQ(evaluate_deal(back, 2, 7).trades, evaluate_deal(bot, 2, 7).trades);
}

}  // namespace
}  // namespace cae
