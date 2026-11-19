#pragma once

// Review of the initial placement: for every settlement a seat places in the
// setup, the chance to win from every corner it could have taken instead.
//
// A corner is scored by Monte Carlo (winrate.hpp): the settlement goes
// there, and from that position zoo bots play the game out, the road of the
// settlement included. The seat's share of wins is the corner's value.
//
// About 50 corners are legal for the first settlement, and the good ones
// differ by a few percentage points, close to the noise of a few hundred
// playouts. Playing every corner long enough to tell the best ones apart
// would cost 50 times the playouts that matter. Successive halving spends
// them where the decision is:
//
//   round 0   every corner gets `first_playouts` playouts
//   round 1   the better half gets twice as many more
//   round 2   the better half of those gets twice as many again ...
//   ...       until `finalists` corners are left
//   confirm   every finalist gets `confirm_playouts` fresh playouts
//
// The corner the player took is never dropped: it is the one being graded,
// so its estimate must be as good as the best one's.
//
// Why a separate confirmation round. The finalists are the corners whose
// estimates came out highest, and some of that is noise that happened to
// favour them: the maximum of noisy estimates is biased upwards (the
// winner's curse of MAP-Elites, docs/map-elites.md). Playouts that played no
// part in choosing the best corner are unbiased for it. So the best corner
// is the finalist ranked first before the confirmation, and its value, like
// the value of the corner played, comes from the confirmation alone.
//
// Every candidate plays the same playout streams (seed, i), as
// play_out_chances does for any position, so the review of a game is the
// same in any thread, chunk or browser.

#include "catan/game.hpp"
#include "catan/winrate.hpp"
#include "catan/zoo.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace cae {

// A settlement placed in the setup of a recorded game.
struct SetupMove {
  int state = 0;  // the state just before it (ReplayLog index)
  int seat = 0;
  int node = 0;   // where it went
  int round = 0;  // 0 for the first settlement of the seat, 1 for the second
};

// Every setup settlement of a game, in order (8 in a game of four).
[[nodiscard]] std::vector<SetupMove> setup_moves(std::span<const Game> states);

struct SetupReviewConfig {
  int first_playouts = 100;   // per corner in round 0; doubles every round
  int finalists = 4;          // halving stops at this many (plus the corner played)
  int confirm_playouts = 1600;
  std::uint64_t seed = 1;
};

// Playout indices of the confirmation, far above those of the halving so the
// two never share a stream.
inline constexpr int kConfirmFirstPlayout = 1 << 24;

struct SetupCandidate {
  int node = 0;
  WinCounts halving;   // all playouts of the halving rounds
  WinCounts confirm;   // the confirmation round, finalists only
  int rounds = 0;      // halving rounds it took part in
  bool dropped = false;  // left in the halving
  bool finalist = false;
};

class SetupReview {
 public:
  // `before` is the state in which the seat places its settlement (phase
  // setup, no road pending). `played` is the corner it took, or -1 to review
  // a position no one has played yet. Playouts draw every seat from `pool`
  // (empty: a random zoo bot), the seat itself included: the review asks
  // how good the corner is for a player of the pool's strength.
  SetupReview(const Game& before, int played, SetupReviewConfig config = {}, std::span<const ZooBot> pool = {});

  // One round: a halving round, or the confirmation when the halving is
  // over. Does nothing once done().
  void step();
  [[nodiscard]] bool done() const noexcept { return done_; }
  // Rounds run so far, the confirmation included.
  [[nodiscard]] int rounds() const noexcept { return round_; }
  [[nodiscard]] long long playouts() const noexcept { return playouts_; }

  [[nodiscard]] int seat() const noexcept { return seat_; }
  [[nodiscard]] const std::vector<SetupCandidate>& candidates() const noexcept { return candidates_; }

  // The value shown for a candidate: its confirmation if it has one, else
  // its halving estimate (rougher for the corners dropped early).
  [[nodiscard]] double value(int candidate) const noexcept;
  [[nodiscard]] int value_playouts(int candidate) const noexcept;

  // After done(): the best corner (index into candidates()) and the corner
  // played (-1 if none or not legal).
  [[nodiscard]] int best() const noexcept { return best_; }
  [[nodiscard]] int played() const noexcept { return played_; }

  // After done(): how much the corner played loses against the best one,
  // in chance to win, from the confirmation playouts alone. Negative when
  // the played corner did better in the confirmation (it is as good as the
  // best). `error` is the standard error of that difference, counting the
  // two estimates as independent; the shared playout streams make them
  // positively correlated, so the true error is a bit smaller.
  struct Loss {
    double loss = 0.0;
    double error = 0.0;
    // The same difference from the halving playouts, which chose the best
    // corner: biased by the winner's curse. Kept to measure that bias.
    double naive = 0.0;
  };
  [[nodiscard]] Loss loss() const noexcept;

 private:
  [[nodiscard]] Game after(int node) const;
  void play(SetupCandidate& c, int first, int count, WinCounts& into);
  [[nodiscard]] std::vector<int> alive() const;
  // Ranking by the halving estimate, best first; ties go to the lower node.
  [[nodiscard]] bool better(int a, int b) const noexcept;

  Game before_;
  int seat_ = 0;
  SetupReviewConfig config_;
  std::span<const ZooBot> pool_;
  std::vector<SetupCandidate> candidates_;
  int played_ = -1;
  int best_ = -1;
  int round_ = 0;
  int next_first_ = 0;  // first halving playout index of the next round
  bool halving_over_ = false;
  bool done_ = false;
  long long playouts_ = 0;
};

}  // namespace cae
