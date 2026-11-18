// Win chances by Monte Carlo (winrate.hpp): where along a game they are
// measured, and that playouts split into chunks add up to one run.

#include "catan/winrate.hpp"

#include "catan/replay.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace cae {
namespace {

ReplayLog open(const std::string& name) {
  std::ifstream in(std::string(CATAN_TEST_DATA_DIR) + "/" + name);
  std::stringstream ss;
  ss << in.rdbuf();
  return ReplayLog::from_text(ss.str());
}

TEST(ChancePoints, StartThenEveryRollThenTheEnd) {
  const ReplayLog log = open("zoo-game-00088.json");
  ASSERT_EQ(log.error(), "");
  const std::vector<ChancePoint> points = chance_points(log.states());
  ASSERT_GE(points.size(), 4u);
  EXPECT_EQ(points.front().kind, PointKind::kStart);
  EXPECT_EQ(points.front().state, 0);
  EXPECT_EQ(points.back().kind, PointKind::kEnd);
  EXPECT_EQ(points.back().state, log.state_count() - 1);
  // In between: pairs around each roll, one event apart, the dice not rolled
  // before and rolled after, in the same turn.
  for (std::size_t i = 1; i + 1 < points.size(); i += 2) {
    const ChancePoint& before = points[i];
    const ChancePoint& after = points[i + 1];
    ASSERT_EQ(before.kind, PointKind::kBeforeRoll) << i;
    ASSERT_EQ(after.kind, PointKind::kAfterRoll) << i;
    EXPECT_EQ(after.state, before.state + 1);
    EXPECT_EQ(log.state(before.state).dice_roll, 0);
    EXPECT_NE(log.state(after.state).dice_roll, 0);
    EXPECT_EQ(before.turn, after.turn);
    EXPECT_EQ(before.seat, after.seat);
    if (i > 1) {
      EXPECT_GT(before.turn, points[i - 1].turn);
    }
  }
}

TEST(PlayOutChances, ChunksAddUpToOneRun) {
  const ReplayLog log = open("zoo-game-00088.json");
  const Game& start = log.state(chance_points(log.states())[5].state);
  const WinCounts whole = play_out_chances(start, 0, 60, 7);
  WinCounts parts = play_out_chances(start, 0, 25, 7);
  parts += play_out_chances(start, 25, 35, 7);
  EXPECT_EQ(parts.wins, whole.wins);
  EXPECT_EQ(parts.playouts, 60);
  int total = whole.unfinished;
  for (int w : whole.wins) total += w;
  EXPECT_EQ(total, 60);
  // Another seed is another estimate.
  EXPECT_NE(play_out_chances(start, 0, 60, 8).wins, whole.wins);
}

TEST(PlayOutChances, AFinishedGameNeedsNoPlayouts) {
  const ReplayLog log = open("zoo-game-00088.json");
  const Game& end = log.state(log.state_count() - 1);
  ASSERT_GE(end.winner, 0);
  const WinCounts w = play_out_chances(end, 0, 100, 1);
  EXPECT_EQ(w.wins[static_cast<std::size_t>(end.winner)], 100);
  EXPECT_DOUBLE_EQ(w.share(end.winner), 1.0);
}

TEST(PlayOutChances, TheRecordedBotsCanSit) {
  const ReplayLog log = open("zoo-game-00088.json");
  const Game& start = log.state(chance_points(log.states())[3].state);
  SeatBots hard{};
  for (int s = 0; s < start.player_count; ++s) hard[static_cast<std::size_t>(s)] = hard_bot();
  EXPECT_EQ(play_out_chances(start, 0, 20, 3, hard).playouts, 20);
  EXPECT_NE(play_out_chances(start, 0, 40, 3, hard).wins, play_out_chances(start, 0, 40, 3).wins);
}

}  // namespace
}  // namespace cae
