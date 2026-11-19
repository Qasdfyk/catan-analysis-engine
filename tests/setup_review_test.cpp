// Review of the initial placement (setup_review.hpp): which moves of a game
// are setup settlements, and how successive halving spends its playouts.

#include "catan/setup_review.hpp"

#include "catan/replay.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

// Small enough for a unit test, large enough for several halving rounds.
constexpr SetupReviewConfig kSmall{.first_playouts = 4, .finalists = 3, .confirm_playouts = 16, .seed = 7};

TEST(SetupMoves, EightSettlementsInSnakeOrder) {
  const ReplayLog log = open("zoo-game-00088.json");
  ASSERT_EQ(log.error(), "");
  const std::vector<SetupMove> moves = setup_moves(log.states());
  ASSERT_EQ(moves.size(), 8u);
  const Game& first = log.state(0);
  for (int i = 0; i < 8; ++i) {
    const SetupMove& m = moves[static_cast<std::size_t>(i)];
    // Round the table and back: seats 0 1 2 3 3 2 1 0 of the setup queue.
    EXPECT_EQ(m.seat, first.setup_queue[static_cast<std::size_t>(i)]) << i;
    EXPECT_EQ(m.round, i < 4 ? 0 : 1) << i;
    const Game& before = log.state(m.state);
    EXPECT_EQ(before.phase, Phase::kSetup);
    EXPECT_TRUE(contains(before.legal_settlements(m.seat), m.node)) << "the corner taken was legal";
    EXPECT_TRUE(contains(log.state(m.state + 1).players[static_cast<std::size_t>(m.seat)].settlements, m.node));
  }
}

TEST(SetupReview, HalvesUntilTheFinalistsThenConfirms) {
  const ReplayLog log = open("zoo-game-00088.json");
  const SetupMove move = setup_moves(log.states())[0];
  SetupReview review(log.state(move.state), move.node, kSmall);
  const int legal = count(log.state(move.state).legal_settlements(move.seat));
  ASSERT_EQ(static_cast<int>(review.candidates().size()), legal);
  ASSERT_GE(review.played(), 0);

  long long expected = 0;
  int in = legal;  // corners in the coming round
  int rounds = 0;
  while (true) {
    review.step();
    // Every corner still in got first << round playouts.
    expected += static_cast<long long>(in) * (kSmall.first_playouts << rounds);
    EXPECT_EQ(review.playouts(), expected) << "round " << rounds;
    ++rounds;
    // Then the better half stays, plus the corner played if it was not in it.
    const int keep = std::max(kSmall.finalists, (in + 1) / 2);
    int left = 0;
    for (const SetupCandidate& c : review.candidates()) left += c.dropped ? 0 : 1;
    EXPECT_TRUE(left == keep || left == keep + 1) << left << " left of " << in;
    EXPECT_FALSE(review.candidates()[static_cast<std::size_t>(review.played())].dropped);
    in = left;
    if (keep <= kSmall.finalists) break;
  }
  EXPECT_FALSE(review.done());
  review.step();  // the confirmation
  EXPECT_TRUE(review.done());
  EXPECT_EQ(review.rounds(), rounds + 1);

  int finalists = 0;
  for (const SetupCandidate& c : review.candidates()) {
    EXPECT_EQ(c.finalist, c.confirm.playouts > 0);
    if (c.finalist) {
      ++finalists;
      EXPECT_EQ(c.confirm.playouts, kSmall.confirm_playouts);
    }
  }
  // The finalists, plus the corner played if halving would have dropped it.
  EXPECT_GE(finalists, kSmall.finalists);
  EXPECT_LE(finalists, kSmall.finalists + 1);
  EXPECT_TRUE(review.candidates()[static_cast<std::size_t>(review.played())].finalist);
  EXPECT_TRUE(review.candidates()[static_cast<std::size_t>(review.best())].finalist);
}

TEST(SetupReview, BestIsChosenByTheHalvingAndValuedByTheConfirmation) {
  const ReplayLog log = open("zoo-game-00088.json");
  const SetupMove move = setup_moves(log.states())[2];
  SetupReview review(log.state(move.state), move.node, kSmall);
  while (!review.done()) review.step();
  const auto& cs = review.candidates();
  const SetupCandidate& best = cs[static_cast<std::size_t>(review.best())];
  for (const SetupCandidate& c : cs) {
    if (c.finalist) {
      EXPECT_GE(best.halving.share(move.seat), c.halving.share(move.seat));
    }
  }
  const SetupReview::Loss loss = review.loss();
  const SetupCandidate& played = cs[static_cast<std::size_t>(review.played())];
  EXPECT_DOUBLE_EQ(loss.loss, best.confirm.share(move.seat) - played.confirm.share(move.seat));
  EXPECT_DOUBLE_EQ(loss.naive, best.halving.share(move.seat) - played.halving.share(move.seat));
  EXPECT_GT(loss.error, 0.0);
}

TEST(SetupReview, SameSeedSameReview) {
  const ReplayLog log = open("zoo-game-00088.json");
  const SetupMove move = setup_moves(log.states())[5];
  SetupReview a(log.state(move.state), move.node, kSmall);
  SetupReview b(log.state(move.state), move.node, kSmall);
  while (!a.done()) a.step();
  while (!b.done()) b.step();
  ASSERT_EQ(a.candidates().size(), b.candidates().size());
  for (std::size_t i = 0; i < a.candidates().size(); ++i) {
    EXPECT_EQ(a.candidates()[i].halving.wins, b.candidates()[i].halving.wins);
    EXPECT_EQ(a.candidates()[i].confirm.wins, b.candidates()[i].confirm.wins);
  }
  EXPECT_EQ(a.best(), b.best());
}

TEST(SetupReview, AFewCornersGoStraightToTheConfirmation) {
  // After one halving round the finalists are all there are.
  const ReplayLog log = open("zoo-game-00088.json");
  const SetupMove move = setup_moves(log.states())[0];
  SetupReview review(log.state(move.state), move.node, {.first_playouts = 2, .finalists = 60, .confirm_playouts = 4});
  review.step();
  EXPECT_FALSE(review.done());
  review.step();
  EXPECT_TRUE(review.done());
  for (const SetupCandidate& c : review.candidates()) EXPECT_TRUE(c.finalist);
}

}  // namespace
}  // namespace cae
