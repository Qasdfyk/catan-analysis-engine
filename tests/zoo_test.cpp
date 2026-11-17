// The zoo bots: how they are made, that they play whole games, and that
// they read records. Whether they play like the Go bots is checked by
// catan_zoo_check (the zoo_matches_go tests below run it on tests/data, and
// scripts/check.sh on every tournament record catan-project has).

#include "catan/random_player.hpp"
#include "catan/record.hpp"
#include "catan/zoo.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace cae {
namespace {

constexpr std::size_t at(int i) { return static_cast<std::size_t>(i); }

const std::string kData = CATAN_TEST_DATA_DIR;

TEST(ZooBot, HardIsBalancedFairUnshaken) {
  const ZooBot hard = hard_bot();
  EXPECT_EQ(hard.archetype, Archetype::kBalanced);
  EXPECT_EQ(hard.personality, Personality::kFair);
  for (double w : hard.weights.resource) EXPECT_EQ(w, 1.0);
  EXPECT_EQ(hard.weights.road_reach, 2);
  EXPECT_EQ(hard.trading.offers_per_turn, 2);
  EXPECT_EQ(hard.trading.embargo, 1);
}

TEST(ZooBot, SeedZeroShakesNothing) {
  const ZooBot bot = make_zoo_bot(Archetype::kOreWheat, Personality::kWary, 0);
  const ZooWeights base = archetype_weights(Archetype::kOreWheat);
  EXPECT_EQ(bot.weights.resource, base.resource);
  EXPECT_EQ(bot.weights.city, base.city);
  EXPECT_EQ(bot.weights.dev_card, base.dev_card);
}

TEST(ZooBot, SameSeedSameBotOtherSeedOtherBot) {
  const ZooBot a = make_zoo_bot(Archetype::kPort, Personality::kFriendly, 1234);
  const ZooBot b = make_zoo_bot(Archetype::kPort, Personality::kFriendly, 1234);
  const ZooBot c = make_zoo_bot(Archetype::kPort, Personality::kFriendly, 1235);
  EXPECT_EQ(a.weights.resource, b.weights.resource);
  EXPECT_EQ(a.weights.port, b.weights.port);
  EXPECT_NE(a.weights.resource, c.weights.resource);
  // The road reach and the trading are not shaken
  EXPECT_EQ(a.weights.road_reach, archetype_weights(Archetype::kPort).road_reach);
  EXPECT_EQ(a.trading.offers_per_turn, 3);
}

TEST(ZooBot, ShakingHasTheSpreadOfTheGoZoo) {
  // log(factor) should have mean 0 and standard deviation 0.2, as in zoo.go.
  double sum = 0;
  double squares = 0;
  int n = 0;
  for (std::uint64_t seed = 1; seed <= 4000; ++seed) {
    const ZooBot bot = make_zoo_bot(Archetype::kBalanced, Personality::kFair, seed);
    for (double w : bot.weights.resource) {
      const double x = std::log(w);
      sum += x;
      squares += x * x;
      ++n;
    }
  }
  const double mean = sum / n;
  const double sd = std::sqrt(squares / n - mean * mean);
  EXPECT_NEAR(mean, 0.0, 0.01);
  EXPECT_NEAR(sd, 0.2, 0.01);
}

TEST(ZooBot, RandomBotsCoverTheZoo) {
  Rng rng(5);
  std::array<int, kArchetypeCount> archetypes{};
  std::array<int, kPersonalityCount> personalities{};
  for (int i = 0; i < 400; ++i) {
    const ZooBot bot = random_zoo_bot(rng);
    EXPECT_NE(bot.seed, 0U);
    EXPECT_LT(bot.seed, std::uint64_t{1} << 53);
    ++archetypes[at(static_cast<int>(bot.archetype))];
    ++personalities[at(static_cast<int>(bot.personality))];
  }
  for (int n : archetypes) EXPECT_GT(n, 60);
  for (int n : personalities) EXPECT_GT(n, 60);
}

TEST(ZooGame, WholeGamesWithoutARefusedMove) {
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    Rng rng(seed);
    ZooTable table;
    table.game = random_game(rng, 2 + static_cast<int>(seed % 3));
    for (auto& bot : table.bots) bot = random_zoo_bot(rng);
    const ZooPlayout r = play_out_zoo(table, rng);
    EXPECT_GE(r.winner, 0) << "seed " << seed;
    EXPECT_EQ(r.fallbacks, 0) << "seed " << seed;
    EXPECT_TRUE(table.game.game_over);
  }
}

TEST(ZooGame, SameSeedSameGame) {
  const auto play = [](std::uint64_t seed) {
    Rng rng(seed);
    ZooTable table;
    table.game = random_game(rng);
    for (auto& bot : table.bots) bot = random_zoo_bot(rng);
    const ZooPlayout r = play_out_zoo(table, rng);
    return std::pair{r.winner, r.actions};
  };
  EXPECT_EQ(play(77), play(77));
  EXPECT_NE(play(77), play(78));
}

TEST(ZooGame, NobodyActsAfterTheEnd) {
  Rng rng(3);
  ZooTable table;
  table.game = random_game(rng);
  for (auto& bot : table.bots) bot = hard_bot();
  play_out_zoo(table, rng);
  EXPECT_EQ(next_actor(table.game), -1);
  EXPECT_EQ(play_zoo_step(table, rng).error, Error::kGameOver);
  for (int s = 0; s < table.game.player_count; ++s) EXPECT_FALSE(zoo_decide(table.game, s, hard_bot(), {}));
}

TEST(ZooGame, DecisionsAreLegalMovesOfTheSeat) {
  // Every move the bot picks must be accepted by the rules, in every
  // position of a long game (play_zoo_step would hide a refusal behind its
  // fallback, so apply the decisions by hand here).
  Rng rng(11);
  ZooTable table;
  table.game = random_game(rng);
  for (auto& bot : table.bots) bot = random_zoo_bot(rng);
  for (int step = 0; step < 3000 && !table.game.game_over; ++step) {
    const int seat = next_actor(table.game);
    table.memory.sync(table.game);
    const auto action = zoo_decide(table.game, seat, table.bots[at(seat)], table.memory);
    ASSERT_TRUE(action) << "step " << step << " seat " << seat;
    ASSERT_EQ(apply_action(table.game, seat, *action, rng), Error::kNone)
        << "step " << step << " " << action_name(action->kind);
    table.memory.record(seat, *action);
  }
}

TEST(ZooRecord, BotsAreRebuiltFromTheirShakenWeights) {
  const auto record = read_record_file(kData + "/zoo-game-00088.json");
  const auto& player = record["players"][0];
  const auto bot = zoo_bot_from_record(player);
  ASSERT_TRUE(bot);
  EXPECT_EQ(archetype_name(bot->archetype), player["bot_style"]["archetype"].get<std::string>());
  EXPECT_EQ(bot->weights.resource[at(static_cast<int>(Resource::kOre))],
            player["bot_style"]["weights"]["resource"]["ore"].get<double>());
  EXPECT_EQ(bot->trading.embargo, player["bot_style"]["trading"]["embargo"].get<int>());

  const auto hard = read_record_file(kData + "/hard-game-00030.json");
  EXPECT_TRUE(zoo_bot_from_record(hard["players"][0]));
  // The random player of a simulation record is no zoo bot
  const auto sim = read_record_file(kData + "/simulation-seed1.json");
  EXPECT_FALSE(zoo_bot_from_record(sim["players"][0]));
}

TEST(ZooRecord, EveryEventParsesAsAnAction) {
  for (const char* name : {"/zoo-game-00088.json", "/simulation-seed2.json"}) {
    const auto record = read_record_file(kData + name);
    for (const auto& event : record["events"]) {
      EXPECT_TRUE(action_from_event(event)) << event.dump();
    }
  }
}

TEST(ServerOrder, StartsWhereThePlatformStarts) {
  // The first corner and side catan-project lists (see kServerNodes).
  EXPECT_EQ(server_node_order()[0], node_at(-2, -1, 1));
  EXPECT_EQ(server_edge_order()[0], edge_at(-2, 1, 5));
}

}  // namespace
}  // namespace cae
