// ReplayLog, the part of the web viewer that is plain C++: every state of a
// recorded game, and the JSON the page draws from.

#include "catan/replay.hpp"

#include "catan/record.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace cae {
namespace {

std::string read_text(const std::string& name) {
  std::ifstream in(std::string(CATAN_TEST_DATA_DIR) + "/" + name);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

TEST(ReplayLog, KeepsEveryStateOfAWholeGame) {
  const std::string text = read_text("simulation-seed1.json");
  const ReplayLog log = ReplayLog::from_text(text);
  ASSERT_EQ(log.error(), "");
  EXPECT_EQ(log.state_count(), log.event_count() + 1);

  // State i must be exactly the game after i events, replayed the long way.
  const auto record = nlohmann::json::parse(text);
  Game g = game_from_record(record);
  EXPECT_EQ(snapshot(log.state(0)), snapshot(g));
  for (int i = 0; i < log.event_count(); ++i) {
    ASSERT_EQ(apply_event(g, record["events"][static_cast<std::size_t>(i)]), Error::kNone);
    ASSERT_EQ(snapshot(log.state(i + 1)), snapshot(g)) << "after event " << i;
  }
  EXPECT_TRUE(log.state(log.state_count() - 1).game_over);
}

TEST(ReplayLog, StateJsonAddsBankAndDice) {
  const ReplayLog log = ReplayLog::from_text(read_text("simulation-seed2.json"));
  ASSERT_EQ(log.error(), "");
  const auto first = log.state_json(0);
  EXPECT_EQ(first["bank"], nlohmann::json({19, 19, 19, 19, 19}));
  EXPECT_TRUE(first["dice"].is_null());
  // Some state after the setup has a roll on it.
  bool saw_dice = false;
  for (int i = 0; i < log.state_count() && !saw_dice; ++i) {
    const auto s = log.state_json(i);
    if (!s["dice"].is_null()) {
      saw_dice = true;
      EXPECT_EQ(s["dice"][0].get<int>() + s["dice"][1].get<int>(), s["dice_roll"].get<int>());
    }
  }
  EXPECT_TRUE(saw_dice);
}

TEST(ReplayLog, BoardJsonHasEveryHexAndPort) {
  const ReplayLog log = ReplayLog::from_text(read_text("simulation-seed3.json"));
  const auto board = log.board_json();
  ASSERT_EQ(board["hexes"].size(), static_cast<std::size_t>(kHexCount));
  int deserts = 0;
  for (const auto& h : board["hexes"]) {
    if (h["terrain"] == "desert") {
      ++deserts;
      EXPECT_EQ(h["number"], 0);
    }
  }
  EXPECT_EQ(deserts, 1);
  EXPECT_EQ(board["ports"].size(), 9U);
  for (const auto& p : board["ports"]) EXPECT_EQ(p["nodes"].size(), 2U);
}

TEST(ReplayLog, BrokenInputGivesAMessageNotACrash) {
  EXPECT_NE(ReplayLog::from_text("not json").error(), "");
  EXPECT_NE(ReplayLog::from_text("{}").error(), "");
  const ReplayLog empty = ReplayLog::from_text("{}");
  EXPECT_EQ(empty.state_count(), 0);
  EXPECT_TRUE(empty.board_json().is_null());
}

TEST(ReplayLog, StopsAtARefusedEvent) {
  auto record = nlohmann::json::parse(read_text("simulation-seed1.json"));
  // Ending the turn before the first settlement is not allowed.
  auto& events = record["events"];
  // json::object() spelled out: a bare braced list here would become an array.
  const auto end_turn = nlohmann::json::object(
      {{"turn", 0}, {"seat", 0}, {"action", "end_turn"}, {"payload", nlohmann::json::object()}});
  events.insert(events.begin() + 2, end_turn);
  const ReplayLog log = ReplayLog::from_text(record.dump());
  EXPECT_NE(log.error().find("event 2 (end_turn) refused"), std::string::npos) << log.error();
  EXPECT_EQ(log.state_count(), 3);  // the start and the two good events
}

TEST(Topology, JsonMatchesTheTables) {
  const auto t = topology_json();
  ASSERT_EQ(t["hexes"].size(), static_cast<std::size_t>(kHexCount));
  ASSERT_EQ(t["nodes"].size(), static_cast<std::size_t>(kNodeCount));
  ASSERT_EQ(t["edges"].size(), static_cast<std::size_t>(kEdgeCount));
  // Every corner's spelling leads back to the same corner.
  for (int n = 0; n < kNodeCount; ++n) {
    const auto& s = t["nodes"][static_cast<std::size_t>(n)];
    EXPECT_EQ(node_at(s["q"].get<int>(), s["r"].get<int>(), s["direction"].get<int>()), n);
  }
}

}  // namespace
}  // namespace cae
