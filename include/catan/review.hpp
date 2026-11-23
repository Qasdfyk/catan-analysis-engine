#pragma once

// The move review (step 13 of the plan): every decision of a player graded
// by how much of their chance to win it gave away against the best move.
//
// A decision is graded the way a golden position is labelled (golden.hpp),
// only with fewer playouts: the moves the searching bot would consider
// (mcts.hpp) plus the move played are narrowed by successive halving, and
// the best of them and the move played get fresh confirmation playouts.
// Every playout draws the hidden cards from what the player could know at
// that moment (HandBelief, belief.hpp), never from the record's true hands,
// so a move is judged on what the player saw, not on what they could not.
//
//   loss   value of the best move - value of the move played, both from the
//          confirmation, in chance to win (0.05 = 5 percentage points)
//   error  the standard error of that difference. Playout i of both moves
//          draws the same hidden cards and dice, so the error comes from the
//          pairs (the spread of won_best[i] - won_played[i]), which is
//          smaller than treating the two values as independent whenever
//          the two moves lead to similar games
//
// The grade:
//   best        the move played is the one the halving ranked first
//   good        the loss is below `inaccuracy`, or within the noise: less
//               than `z` errors above zero, so the playouts cannot tell the
//               two moves apart
//   inaccuracy  loss in [inaccuracy, mistake)
//   mistake     loss in [mistake, blunder)
//   blunder     loss of `blunder` or more
// The thresholds were set on games between bots (docs/move-review.md) and
// are to be checked on games of people (set H of the plan).
//
// Which decisions: the ones the searching bot would search (the turn, the
// robber, the victim, offers it would make) where there is more than one
// move. Answering offers and discarding are left out, like in the search.
// The setup is left out too by default: the setup review (setup_review.hpp)
// grades it with far more playouts.

#include "catan/expert.hpp"
#include "catan/game.hpp"
#include "catan/golden.hpp"
#include "catan/zoo.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cae {

enum class Grade : std::uint8_t { kBest, kGood, kInaccuracy, kMistake, kBlunder };
inline constexpr int kGradeCount = 5;

[[nodiscard]] std::string_view grade_name(Grade g) noexcept;

struct GradeThresholds {
  // Losses in chance to win at which a grade starts.
  double inaccuracy = 0.03;
  double mistake = 0.06;
  double blunder = 0.12;
  double z = 1.96;  // a loss less than z errors above zero is within the noise
};

struct ReviewConfig {
  GoldenConfig label{.first_playouts = 32, .finalists = 2, .confirm_playouts = 800, .seed = 1};
  GradeThresholds thresholds;
  bool setup = false;  // grade the setup too (the setup review does it better)
};

// A decision of a recorded game, with what its player knew.
struct ReviewDecision {
  int state = 0;  // index of the state before the move (ReplayLog: state i is after event i - 1)
  int seat = 0;
  Game game;      // the true state; only `known` of it reaches the playouts
  std::array<Cards, kMaxPlayers> known{};
  TableMemory memory;
  Action played;
};

// Every decision of `seat` (or of every seat, -1) in the record that the
// review grades, in order. Walks the record once. Throws RecordError like
// table_from_record when the record does not replay.
[[nodiscard]] std::vector<ReviewDecision> review_decisions(const nlohmann::json& record, int seat = -1,
                                                           bool setup = false);

struct MoveReview {
  int state = 0;
  int seat = 0;
  int turn = 0;
  Action played;
  Action best;
  double played_value = 0;
  double best_value = 0;
  double loss = 0;           // best_value - played_value (can be below zero: noise)
  double error = 0;          // paired standard error of the loss
  double error_unpaired = 0; // the same, treating the two values as independent
  Grade grade = Grade::kBest;
  bool within_noise = false; // not best, and the loss is within z errors of zero
  int candidates = 0;
  long long playouts = 0;
};

// The grade of a loss with its error.
[[nodiscard]] Grade grade_of(double loss, double error, bool played_is_best,
                             const GradeThresholds& thresholds) noexcept;

// Grades one decision. The opponents in the playouts are drawn from `pool`
// (the MAP-Elites; empty means hard), the player is the hard bot after its
// move. The same arguments give the same review.
[[nodiscard]] MoveReview review_decision(const ReviewDecision& d, const ReviewConfig& config,
                                         std::span<const ZooBot> pool);

// A review as JSON: {"state", "seat", "turn", "played", "best", "played_value",
// "best_value", "loss", "error", "grade", "within_noise", "candidates"}.
[[nodiscard]] nlohmann::json review_json(const MoveReview& r);

// How a player did over a game: decisions per grade and the mean loss
// (losses below zero count as zero).
struct ReviewSummary {
  int decisions = 0;
  std::array<int, kGradeCount> grades{};
  double mean_loss = 0;
};
[[nodiscard]] ReviewSummary summarize(std::span<const MoveReview> reviews);

}  // namespace cae
