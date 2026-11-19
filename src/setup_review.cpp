#include "catan/setup_review.hpp"

#include "catan/bits.hpp"

#include <algorithm>
#include <cmath>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

}  // namespace

std::vector<SetupMove> setup_moves(std::span<const Game> states) {
  std::vector<SetupMove> moves;
  std::array<int, kMaxPlayers> placed{};
  for (std::size_t i = 0; i + 1 < states.size(); ++i) {
    const Game& g = states[i];
    if (g.phase != Phase::kSetup || g.setup_waiting_for_road) continue;
    // The one event in the setup that leaves a road pending is a settlement.
    const Game& next = states[i + 1];
    if (!next.setup_waiting_for_road) continue;
    const int seat = g.current;
    moves.push_back({static_cast<int>(i), seat, next.setup_last_node, placed[at(seat)]++});
  }
  return moves;
}

SetupReview::SetupReview(const Game& before, int played, SetupReviewConfig config, std::span<const ZooBot> pool)
    : before_(before), seat_(before.current), config_(config), pool_(pool) {
  for_each_node(before.legal_settlements(seat_), [&](int node) {
    if (node == played) played_ = static_cast<int>(candidates_.size());
    SetupCandidate c;
    c.node = node;
    candidates_.push_back(c);
  });
  done_ = candidates_.empty();
}

Game SetupReview::after(int node) const {
  Game g = before_;
  // Legal by construction: the node came from legal_settlements.
  (void)g.build_settlement(seat_, node);
  return g;
}

void SetupReview::play(SetupCandidate& c, int first, int count, WinCounts& into) {
  into += play_out_chances(after(c.node), first, count, config_.seed, {}, pool_);
  playouts_ += count;
}

std::vector<int> SetupReview::alive() const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(candidates_.size()); ++i) {
    if (!candidates_[at(i)].dropped) out.push_back(i);
  }
  return out;
}

bool SetupReview::better(int a, int b) const noexcept {
  const double va = candidates_[at(a)].halving.share(seat_);
  const double vb = candidates_[at(b)].halving.share(seat_);
  if (va != vb) return va > vb;
  return candidates_[at(a)].node < candidates_[at(b)].node;
}

void SetupReview::step() {
  if (done_) return;
  std::vector<int> live = alive();

  if (halving_over_) {
    // Confirmation: fresh playouts for every finalist.
    for (const int i : live) {
      SetupCandidate& c = candidates_[at(i)];
      play(c, kConfirmFirstPlayout, config_.confirm_playouts, c.confirm);
    }
    // The best corner was chosen before these playouts, so they are unbiased for it.
    best_ = *std::min_element(live.begin(), live.end(), [&](int a, int b) { return better(a, b); });
    ++round_;
    done_ = true;
    return;
  }

  // A halving round: the same playouts for every corner still in.
  const int count = config_.first_playouts << round_;
  for (const int i : live) {
    SetupCandidate& c = candidates_[at(i)];
    play(c, next_first_, count, c.halving);
    ++c.rounds;
  }
  next_first_ += count;
  ++round_;

  // Keep the better half, never fewer than the finalists, and the corner played.
  std::sort(live.begin(), live.end(), [&](int a, int b) { return better(a, b); });
  const int target = std::max(config_.finalists, static_cast<int>(live.size() + 1) / 2);
  const auto keep = static_cast<std::size_t>(std::min<int>(target, static_cast<int>(live.size())));
  for (std::size_t k = keep; k < live.size(); ++k) {
    if (live[k] != played_) candidates_[at(live[k])].dropped = true;
  }
  if (static_cast<int>(keep) <= config_.finalists) {
    halving_over_ = true;
    for (const int i : alive()) candidates_[at(i)].finalist = true;
  }
}

double SetupReview::value(int candidate) const noexcept {
  const SetupCandidate& c = candidates_[at(candidate)];
  return c.confirm.playouts > 0 ? c.confirm.share(seat_) : c.halving.share(seat_);
}

int SetupReview::value_playouts(int candidate) const noexcept {
  const SetupCandidate& c = candidates_[at(candidate)];
  return c.confirm.playouts > 0 ? c.confirm.playouts : c.halving.playouts;
}

SetupReview::Loss SetupReview::loss() const noexcept {
  Loss out;
  if (!done_ || played_ < 0 || best_ < 0) return out;
  const SetupCandidate& b = candidates_[at(best_)];
  const SetupCandidate& p = candidates_[at(played_)];
  const double vb = b.confirm.share(seat_);
  const double vp = p.confirm.share(seat_);
  out.loss = vb - vp;
  const auto variance = [](double v, int n) { return n > 0 ? v * (1.0 - v) / n : 0.0; };
  out.error = std::sqrt(variance(vb, b.confirm.playouts) + variance(vp, p.confirm.playouts));
  out.naive = b.halving.share(seat_) - p.halving.share(seat_);
  return out;
}

}  // namespace cae
