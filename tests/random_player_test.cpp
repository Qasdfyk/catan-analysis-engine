// The random board, the random player and the generator behind them.

#include "catan/random_player.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace cae {
namespace {

constexpr std::size_t at(int i) { return static_cast<std::size_t>(i); }

TEST(Rng, SameSeedSameStream) {
  Rng a(42);
  Rng b(42);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(a.next(), b.next());
}

TEST(Rng, NeighboringSeedsDiffer) {
  Rng a(1);
  Rng b(2);
  EXPECT_NE(a.next(), b.next());
}

TEST(Rng, BelowStaysInRangeAndHitsEveryValue) {
  Rng rng(7);
  for (int n : {1, 2, 3, 6, 19, 54, 72}) {
    std::array<int, 72> seen{};
    for (int i = 0; i < 20'000; ++i) {
      const int x = rng.below(n);
      ASSERT_GE(x, 0);
      ASSERT_LT(x, n);
      ++seen[at(x)];
    }
    for (int x = 0; x < n; ++x) EXPECT_GT(seen[at(x)], 0) << "n=" << n << " x=" << x;
  }
}

TEST(Rng, DiceAreRoughlyFair) {
  // 60 000 rolls, 10 000 expected per face; the standard deviation is about
  // 91, so 500 is more than five of them: a fair die fails this practically never.
  Rng rng(3);
  std::array<int, 7> faces{};
  for (int i = 0; i < 60'000; ++i) ++faces[at(rng.die())];
  EXPECT_EQ(faces[0], 0);
  for (int f = 1; f <= 6; ++f) EXPECT_NEAR(faces[at(f)], 10'000, 500) << "face " << f;
}

TEST(RandomBoard, HasTheStandardPieces) {
  Rng rng(5);
  for (int round = 0; round < 50; ++round) {
    const Board b = random_board(rng);
    std::array<int, 6> terrain{};
    std::array<int, 13> numbers{};
    for (int h = 0; h < kHexCount; ++h) {
      ++terrain[static_cast<std::size_t>(b.terrain[at(h)])];
      ++numbers[at(b.number[at(h)])];
      EXPECT_EQ(b.number[at(h)] == 0, b.terrain[at(h)] == Terrain::kDesert);
    }
    EXPECT_EQ(terrain, (std::array<int, 6>{4, 3, 4, 4, 3, 1}));  // wood brick sheep wheat ore desert
    EXPECT_EQ(numbers, (std::array<int, 13>{1, 0, 1, 2, 2, 2, 2, 0, 2, 2, 2, 2, 1}));  // [0] = the desert
    EXPECT_EQ(b.port_count, kMaxPorts);
  }
}

TEST(RandomBoard, RedNumbersNeverTouch) {
  Rng rng(6);
  for (int round = 0; round < 200; ++round) {
    const Board b = random_board(rng);
    for (int h = 0; h < kHexCount; ++h) {
      for (int o = h + 1; o < kHexCount; ++o) {
        const bool red_h = b.number[at(h)] == 6 || b.number[at(h)] == 8;
        const bool red_o = b.number[at(o)] == 6 || b.number[at(o)] == 8;
        const bool touch = count(kTopology.hex_node_mask[at(h)] & kTopology.hex_node_mask[at(o)]) == 2;
        EXPECT_FALSE(red_h && red_o && touch) << "hexes " << h << " and " << o;
      }
    }
  }
}

TEST(RandomBoard, PortsSitOnTheCoastAndDoNotShareCorners) {
  Rng rng(8);
  const Board b = random_board(rng);
  NodeMask used = 0;
  std::array<int, 6> types{};
  for (int i = 0; i < b.port_count; ++i) {
    const NodeMask corners = b.ports[at(i)].nodes;
    EXPECT_EQ(count(corners), 2);
    EXPECT_EQ(used & corners, 0U) << "port " << i;
    used |= corners;
    ++types[static_cast<std::size_t>(b.ports[at(i)].type)];
    // A coastal corner touches the sea: one of its three hexes is not land.
    for_each_node(corners, [](int n) {
      const auto& hexes = kTopology.node_hexes[at(n)];
      EXPECT_TRUE(std::any_of(hexes.begin(), hexes.end(), [](std::int8_t h) { return h < 0; })) << "corner " << n;
    });
  }
  EXPECT_EQ(types, (std::array<int, 6>{4, 1, 1, 1, 1, 1}));
}

TEST(RandomDevDeck, HasTheStandardCards) {
  Rng rng(9);
  const auto deck = random_dev_deck(rng);
  std::array<int, kDevCardKinds> kinds{};
  for (DevCard c : deck) ++kinds[static_cast<std::size_t>(c)];
  EXPECT_EQ(kinds, (std::array<int, kDevCardKinds>{14, 5, 2, 2, 2}));
}

TEST(RandomPlayer, FinishesGamesWithoutARefusedAction) {
  for (int players = 2; players <= 4; ++players) {
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
      Rng rng(seed);
      Game g = random_game(rng, players);
      const PlayoutResult r = play_out(g, rng);
      ASSERT_EQ(r.refused, 0) << players << " players, seed " << seed;
      ASSERT_GE(r.winner, 0) << players << " players, seed " << seed;
      EXPECT_GE(g.visible_points(r.winner) + g.hidden_points(r.winner), g.settings.victory_points);
    }
  }
}

TEST(RandomPlayer, SameSeedSameGame) {
  Rng a(11);
  Rng b(11);
  Game ga = random_game(a, 4);
  Game gb = random_game(b, 4);
  (void)play_out(ga, a);
  (void)play_out(gb, b);
  // Game is plain data with no padding we rely on; compare the parts that matter.
  EXPECT_EQ(ga.winner, gb.winner);
  EXPECT_EQ(ga.turn_number, gb.turn_number);
  EXPECT_EQ(ga.roll_counts, gb.roll_counts);
  for (int s = 0; s < 4; ++s) EXPECT_EQ(ga.players[at(s)].resources, gb.players[at(s)].resources);
}

}  // namespace
}  // namespace cae
