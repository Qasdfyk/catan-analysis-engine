// The arena: deals are reproducible, games of a deal share what they should,
// and the statistics behind SPRT are right.

#include "catan/arena.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace cae {
namespace {

std::vector<ArenaPlayer> hard_table() { return std::vector<ArenaPlayer>(4, zoo_player(hard_bot())); }

TEST(Arena, DealsAreReproducible) {
  const Deal a = make_deal(5, 17);
  const Deal b = make_deal(5, 17);
  const Deal c = make_deal(5, 18);
  EXPECT_EQ(a.board.terrain, b.board.terrain);
  EXPECT_EQ(a.board.number, b.board.number);
  EXPECT_EQ(a.deck, b.deck);
  EXPECT_EQ(a.dice_seed, b.dice_seed);
  EXPECT_NE(a.dice_seed, c.dice_seed);
  for (int k = 0; k < 200; ++k) {
    const auto d = deal_dice(a.dice_seed, k);
    EXPECT_EQ(d, deal_dice(a.dice_seed, k));
    EXPECT_GE(d[0], 1);
    EXPECT_LE(d[1], 6);
  }
}

TEST(Arena, SameDealSameGame) {
  const auto table = hard_table();
  const Deal deal = make_deal(1, 3);
  for (const bool fixed : {false, true}) {
    const ArenaGame x = play_arena_game(deal, table, 2, fixed);
    const ArenaGame y = play_arena_game(deal, table, 2, fixed);
    EXPECT_GE(x.winner, 0);
    EXPECT_EQ(x.winner, y.winner);
    EXPECT_EQ(x.actions, y.actions);
    EXPECT_EQ(x.fallbacks, (std::array<int, kMaxPlayers>{}));
  }
}

// With fixed dice the k-th roll of every game of a deal is the deal's k-th
// pair, whatever the game number (the games themselves differ).
TEST(Arena, FixedDiceFollowTheDeal) {
  const Deal deal = make_deal(2, 9);
  for (int game_number = 0; game_number < 4; ++game_number) {
    SCOPED_TRACE(game_number);
    std::vector<std::array<int, 2>> rolls;
    int last_turn = -1;
    std::vector<ArenaPlayer> table;
    for (int s = 0; s < 4; ++s) {
      Rng pick(static_cast<std::uint64_t>(s + 10 * game_number));
      table.push_back([&, inner = zoo_player(random_zoo_bot(pick))](const ArenaView& v) {
        // Every turn rolls once; the first decision after it sees the dice.
        if (v.game.dice_roll > 0 && v.game.turn_number != last_turn) {
          rolls.push_back({v.game.dice[0], v.game.dice[1]});
          last_turn = v.game.turn_number;
        }
        return inner(v);
      });
    }
    (void)play_arena_game(deal, table, game_number, true);
    ASSERT_GT(rolls.size(), 20U);
    for (std::size_t k = 0; k < rolls.size(); ++k) {
      EXPECT_EQ(rolls[k], deal_dice(deal.dice_seed, static_cast<int>(k))) << "roll " << k;
    }
  }
}

TEST(Arena, NoTradesMeansNoOffers) {
  const Deal deal = make_deal(3, 1);
  int offers = 0;
  int accepted = 0;
  std::vector<ArenaPlayer> table;
  for (int s = 0; s < 4; ++s) {
    table.push_back([&, inner = zoo_player(make_zoo_bot(Archetype::kPort, Personality::kFriendly, 7), false)](
                        const ArenaView& v) {
      auto a = inner(v);
      if (a && a->kind == ActionKind::kOfferTrade) ++offers;
      if (a && a->kind == ActionKind::kRespondTrade && a->accept) ++accepted;
      return a;
    });
  }
  const ArenaGame g = play_arena_game(deal, table, 0, false);
  EXPECT_GE(g.winner, 0);
  EXPECT_EQ(offers, 0);
  EXPECT_EQ(accepted, 0);
}

TEST(Arena, HowEachSeatPlayed) {
  const auto table = hard_table();
  for (int i = 0; i < 10; ++i) {
    const ArenaGame g = play_arena_game(make_deal(2, i), table, 0, false);
    int trades = 0;
    for (const SeatPlay& p : g.play) {
      EXPECT_GE(p.settlements + p.cities, 2);  // the two of the setup at least
      EXPECT_LE(p.cities, kMaxCities);
      EXPECT_GE(p.dev_cards, 0);
      trades += p.trades;
    }
    EXPECT_EQ(trades % 2, 0);  // every trade counts for both sides
  }
}

TEST(Arena, ScoreStats) {
  ScoreStats s;
  for (double x : {0.0, 0.25, 0.5, 0.25}) s.add(x);
  EXPECT_DOUBLE_EQ(s.mean(), 0.25);
  // deviations -0.25, 0, 0.25, 0: sum of squares 0.125 over n - 1 = 3
  EXPECT_NEAR(s.variance(), 0.125 / 3, 1e-12);
  EXPECT_NEAR(s.std_error(), std::sqrt(0.125 / 3 / 4), 1e-12);
}

TEST(Arena, SprtGoesTheRightWay) {
  const Sprt test{0.25, 0.30, 0.05, 0.05};
  EXPECT_NEAR(test.upper(), std::log(0.95 / 0.05), 1e-12);
  EXPECT_NEAR(test.lower(), -test.upper(), 1e-12);
  // At the midpoint the evidence is even
  ScoreStats mid;
  for (int i = 0; i < 100; ++i) mid.add(i % 2 == 0 ? 0.0 : 0.55);
  EXPECT_NEAR(test.llr(mid), 0.0, 1e-9);
  ScoreStats strong;
  ScoreStats weak;
  for (int i = 0; i < 400; ++i) {
    strong.add(i % 5 < 2 ? 1.0 : 0.0);  // 40%
    weak.add(i % 10 == 0 ? 1.0 : 0.0);  // 10%
  }
  EXPECT_EQ(sprt_decide(test, strong), SprtResult::kH1);
  EXPECT_EQ(sprt_decide(test, weak), SprtResult::kH0);
  ScoreStats few;
  few.add(1.0);
  EXPECT_EQ(sprt_decide(test, few), SprtResult::kContinue);
}

}  // namespace
}  // namespace cae
