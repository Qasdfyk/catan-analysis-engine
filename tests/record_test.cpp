// Whole recorded games (tests/data, made by catan-project's
// `game_record simulate`) replay through the C++ rules to the recorded
// result, without Python.

#include "catan/record.hpp"
#include "catan/random_player.hpp"
#include "catan/zoo.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace cae {
namespace {

std::vector<std::string> record_files() {
  std::vector<std::string> out;
  for (const auto& entry : std::filesystem::directory_iterator(CATAN_TEST_DATA_DIR)) {
    if (entry.path().extension() == ".json") out.push_back(entry.path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

TEST(Records, TestDataIsThere) { EXPECT_GE(record_files().size(), 3U); }

TEST(Records, ReplayToTheRecordedResult) {
  for (const std::string& path : record_files()) {
    SCOPED_TRACE(path);
    const nlohmann::json record = read_record_file(path);
    Game g = game_from_record(record);
    const auto& events = record.at("events");
    for (std::size_t i = 0; i < events.size(); ++i) {
      ASSERT_EQ(apply_event(g, events[i]), Error::kNone) << "event " << i << ": " << events[i].dump();
    }
    const auto& result = record.at("result");
    EXPECT_TRUE(g.game_over);
    EXPECT_EQ(result.at("winner_seat").get<int>(), g.winner);
    EXPECT_EQ(result.at("turns").get<int>(), g.turn_number);
    for (int s = 0; s < g.player_count; ++s) {
      EXPECT_EQ(result.at("victory_points").at(static_cast<std::size_t>(s)).get<int>(),
                g.visible_points(s) + g.hidden_points(s));
    }
  }
}

TEST(Records, BrokenEventsAreRefusedNotCrashes) {
  const nlohmann::json record = read_record_file(record_files().front());
  Game g = game_from_record(record);
  using nlohmann::json;
  EXPECT_EQ(apply_event(g, json::parse(R"({"seat": 0, "action": "fly"})")), Error::kBadArgument);
  EXPECT_EQ(apply_event(g, json::parse(R"({"seat": 9, "action": "end_turn"})")), Error::kBadArgument);
  EXPECT_EQ(apply_event(g, json::parse(R"({"seat": 0, "action": "build_road", "payload": {"hex": {"q": 0, "r": 0, "s": 1}, "direction": 0}})")),
            Error::kBadArgument);  // q + r + s != 0
  EXPECT_EQ(apply_event(g, json::parse(R"({"seat": 0, "action": "build_road", "payload": {"hex": {"q": 0, "r": 0, "s": 0}, "direction": true}})")),
            Error::kBadArgument);  // a bool is not a direction
  EXPECT_EQ(apply_event(g, json::parse(R"({"seat": 0, "action": "roll_dice", "payload": {}})")), Error::kBadOutcome);
  EXPECT_THROW((void)game_from_record(json::parse(R"({"format": "something else"})")), RecordError);
  EXPECT_THROW((void)game_from_record(json::parse(R"({"format": "catan-game-record", "version": 2})")), RecordError);
}

// A position written as board + snapshot comes back as the same position,
// and a game played on from it goes exactly as from the original.
TEST(Records, SnapshotRoundTrip) {
  int checked = 0;
  for (const std::string& path : record_files()) {
    SCOPED_TRACE(path);
    const nlohmann::json record = read_record_file(path);
    Game g = game_from_record(record);
    const nlohmann::json board = board_json(g.board);
    for (const auto& event : record.at("events")) {
      const nlohmann::json snap = snapshot(g);
      const Game back = game_from_snapshot(board, snap);
      ASSERT_EQ(snapshot(back), snap);
      ASSERT_EQ(apply_event(g, event), Error::kNone);
      ++checked;
    }
  }
  EXPECT_GT(checked, 1000);

  // Zoo games from a few positions mid-game: same moves, same end.
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    SCOPED_TRACE(seed);
    Rng rng(seed);
    ZooTable a;
    a.game = random_game(rng);
    for (auto& bot : a.bots) bot = random_zoo_bot(rng);
    for (int i = 0; i < 150; ++i) (void)play_zoo_step(a, rng);
    ZooTable b = a;
    b.game = game_from_snapshot(board_json(a.game.board), snapshot(a.game));
    Rng ra(seed + 100);
    Rng rb(seed + 100);
    const ZooPlayout pa = play_out_zoo(a, ra);
    const ZooPlayout pb = play_out_zoo(b, rb);
    EXPECT_EQ(pa.winner, pb.winner);
    EXPECT_EQ(pa.actions, pb.actions);
    EXPECT_EQ(snapshot(a.game), snapshot(b.game));
  }
}

TEST(Records, ActionJsonRoundTrip) {
  std::vector<Action> actions;
  for (int k = 0; k <= static_cast<int>(ActionKind::kEndTurn); ++k) {
    Action a;
    a.kind = static_cast<ActionKind>(k);
    switch (a.kind) {
      case ActionKind::kBuildSettlement:
      case ActionKind::kBuildRoad:
      case ActionKind::kUpgradeCity:
      case ActionKind::kMoveRobber:
      case ActionKind::kSteal:
      case ActionKind::kConfirmTrade: a.target = 7; break;
      case ActionKind::kDiscard: a.give = {1, 0, 2, 0, 1}; break;
      case ActionKind::kOfferTrade: a.give = {1, 0, 0, 0, 0}; a.get = {0, 0, 0, 0, 1}; break;
      case ActionKind::kRespondTrade: a.accept = true; break;
      case ActionKind::kTradeWithBank:
      case ActionKind::kPlayYearOfPlenty: a.first = Resource::kSheep; a.second = Resource::kOre; break;
      case ActionKind::kPlayMonopoly: a.first = Resource::kWheat; break;
      default: break;
    }
    actions.push_back(a);
  }
  for (const Action& a : actions) {
    SCOPED_TRACE(action_json(a).dump());
    const auto back = action_from_json(action_json(a));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, a);
  }
  EXPECT_FALSE(action_from_json(nlohmann::json{{"kind", "fly"}}).has_value());
  EXPECT_FALSE(action_from_json(nlohmann::json{{"kind", "play_monopoly"}, {"first", "gold"}}).has_value());
}

}  // namespace
}  // namespace cae
