// Whole recorded games (tests/data, made by catan-project's
// `game_record simulate`) replay through the C++ rules to the recorded
// result, without Python.

#include "catan/record.hpp"

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

}  // namespace
}  // namespace cae
