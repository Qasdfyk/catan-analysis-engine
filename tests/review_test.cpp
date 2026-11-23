// The move review: which decisions it grades and with what knowledge, the
// grade of a loss, a winning move skipped, and a review
// that does not change when only cards the player could not see change.

#include "catan/belief.hpp"
#include "catan/expert.hpp"
#include "catan/record.hpp"
#include "catan/review.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cae {
namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

json read_record(const std::string& name) {
  std::ifstream in(std::string(CATAN_TEST_DATA_DIR) + "/" + name);
  std::stringstream text;
  text << in.rdbuf();
  return json::parse(text.str());
}

ReviewConfig small() {
  ReviewConfig c;
  c.label.first_playouts = 8;
  c.label.finalists = 2;
  c.label.confirm_playouts = 64;
  return c;
}

TEST(Review, DecisionsAreThePlayersOwnWithWhatTheyKnewThen) {
  const json record = read_record("hard-game-00030.json");
  const std::vector<ReviewDecision> all = review_decisions(record, 2);
  ASSERT_GE(all.size(), 10U);
  int last = -1;
  for (const ReviewDecision& d : all) {
    EXPECT_EQ(d.seat, 2);
    EXPECT_GT(d.state, last);
    last = d.state;
    EXPECT_NE(d.game.phase, Phase::kSetup);
    const json& event = record["events"][at(d.state)];
    EXPECT_EQ(event["seat"], 2);
    EXPECT_EQ(action_from_event(event), d.played);
  }
  // The same as replaying the record up to each decision from scratch.
  for (const std::size_t i : {std::size_t{0}, all.size() / 2, all.size() - 1}) {
    const ReviewDecision& d = all[i];
    json cut = record;
    cut["events"] = json(std::vector<json>(record["events"].begin(), record["events"].begin() + d.state));
    const TablePosition table = table_from_record(cut);
    EXPECT_EQ(snapshot(table.game), snapshot(d.game));
    EXPECT_EQ(table.belief.known[2], d.known);
    EXPECT_EQ(memory_json(table.memory), memory_json(d.memory));
  }
  EXPECT_GT(review_decisions(record, 2, true).size(), all.size());  // the setup on top
}

TEST(Review, GradesFollowTheThresholdsAndTheNoise) {
  const GradeThresholds t;  // 3, 6 and 12 pp, z = 1.96
  EXPECT_EQ(grade_of(0.50, 0.01, true, t), Grade::kBest);
  EXPECT_EQ(grade_of(0.02, 0.001, false, t), Grade::kGood);
  EXPECT_EQ(grade_of(0.04, 0.001, false, t), Grade::kInaccuracy);
  EXPECT_EQ(grade_of(0.04, 0.025, false, t), Grade::kGood);  // within 1.96 errors of zero
  EXPECT_EQ(grade_of(0.07, 0.01, false, t), Grade::kMistake);
  EXPECT_EQ(grade_of(0.12, 0.01, false, t), Grade::kBlunder);
  EXPECT_EQ(grade_of(-0.05, 0.01, false, t), Grade::kGood);
}

// The last move of the winner ends the game. Ending the turn instead puts
// off a certain win: not the best move. (Not a blunder: with the cards in
// hand the player mostly wins a round later, so the loss is only a few pp.)
TEST(Review, SkippingTheWinningMoveIsNotBest) {
  const json record = read_record("hard-game-00030.json");
  const int winner = record["result"]["winner_seat"].get<int>();
  const std::vector<ReviewDecision> all = review_decisions(record, winner);
  ASSERT_FALSE(all.empty());
  ReviewDecision d = all.back();
  Game after = d.game;
  Rng rng(1);
  ASSERT_EQ(apply_action(after, d.seat, d.played, rng), Error::kNone);
  ASSERT_TRUE(after.game_over);

  const MoveReview right = review_decision(d, small(), {});
  EXPECT_EQ(right.grade, Grade::kBest);
  EXPECT_DOUBLE_EQ(right.played_value, 1.0);

  d.played = Action{};
  d.played.kind = ActionKind::kEndTurn;
  const MoveReview wrong = review_decision(d, small(), {});
  EXPECT_NE(wrong.grade, Grade::kBest);
  EXPECT_DOUBLE_EQ(wrong.best_value, 1.0);
  EXPECT_LT(wrong.played_value, 1.0);
  EXPECT_NEAR(wrong.loss, 1.0 - wrong.played_value, 1e-12);
  // Against a sure win there is nothing to pair: both errors are the spread
  // of the played move alone (up to n - 1 against n).
  EXPECT_GT(wrong.error, 0.0);
  EXPECT_NEAR(wrong.error, wrong.error_unpaired, 0.001);
}

// Replace every card the player could not see by another draw that agrees
// with what they knew: the review must not move at all.
TEST(Review, CardsThePlayerCouldNotSeeDoNotChangeTheReview) {
  const json record = read_record("zoo-game-00088.json");
  const std::vector<ReviewDecision> all = review_decisions(record, 1);
  ASSERT_GT(all.size(), 10U);
  for (const std::size_t i : {all.size() / 3, 2 * all.size() / 3}) {
    const ReviewDecision& d = all[i];
    ReviewDecision other = d;
    Rng rng(99);
    other.game = determinize(d.game, d.seat, d.known, rng);
    const MoveReview a = review_decision(d, small(), {});
    const MoveReview b = review_decision(other, small(), {});
    EXPECT_EQ(a.best, b.best);
    EXPECT_DOUBLE_EQ(a.played_value, b.played_value);
    EXPECT_DOUBLE_EQ(a.best_value, b.best_value);
    EXPECT_EQ(a.grade, b.grade);
  }
}

TEST(Review, AReviewIsReproducibleAndSummed) {
  const json record = read_record("zoo-game-00206.json");
  std::vector<ReviewDecision> all = review_decisions(record, 0);
  all.resize(std::min<std::size_t>(all.size(), 6));
  std::vector<MoveReview> reviews;
  for (const ReviewDecision& d : all) {
    const MoveReview a = review_decision(d, small(), {});
    const MoveReview b = review_decision(d, small(), {});
    EXPECT_EQ(review_json(a), review_json(b));
    reviews.push_back(a);
  }
  const ReviewSummary s = summarize(reviews);
  EXPECT_EQ(s.decisions, static_cast<int>(reviews.size()));
  int graded = 0;
  for (const int g : s.grades) graded += g;
  EXPECT_EQ(graded, s.decisions);
  EXPECT_GE(s.mean_loss, 0.0);
}

}  // namespace
}  // namespace cae
