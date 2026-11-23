#include "catan/review.hpp"

#include "catan/mcts.hpp"
#include "catan/record.hpp"

#include <algorithm>
#include <cmath>

namespace cae {

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// Standard error of the mean of x[i] - y[i] over the pairs.
double paired_error(const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y) noexcept {
  const std::size_t n = std::min(x.size(), y.size());
  if (n < 2) return 0;
  double sum = 0;
  double squares = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double d = static_cast<double>(x[i]) - static_cast<double>(y[i]);
    sum += d;
    squares += d * d;
  }
  const double count = static_cast<double>(n);
  const double mean = sum / count;
  const double variance = (squares - count * mean * mean) / (count - 1);
  return std::sqrt(std::max(variance, 0.0) / count);
}

double binomial_variance(const WinCounts& w, int seat) noexcept {
  if (w.playouts == 0) return 0;
  const double p = w.share(seat);
  return p * (1 - p) / w.playouts;
}

}  // namespace

std::string_view grade_name(Grade g) noexcept {
  switch (g) {
    case Grade::kBest: return "best";
    case Grade::kGood: return "good";
    case Grade::kInaccuracy: return "inaccuracy";
    case Grade::kMistake: return "mistake";
    case Grade::kBlunder: return "blunder";
  }
  return "?";
}

std::vector<ReviewDecision> review_decisions(const json& record, int seat, bool setup) {
  std::vector<ReviewDecision> out;
  TablePosition p;
  p.game = game_from_record(record);
  if (!record.contains("events") || !record["events"].is_array()) throw RecordError("record has no events");
  for (const json& event : record["events"]) {
    const Game& g = p.game;
    const int actor = event.value("seat", -1);
    if (!g.game_over && actor >= 0 && (seat < 0 || actor == seat) && (setup || g.phase != Phase::kSetup) &&
        searched_decision(g, actor)) {
      const std::optional<Action> played = action_from_event(event);
      if (played && search_candidates(g, actor, std::nullopt).size() > 1) {
        p.memory.sync(g);
        out.push_back({p.events, actor, g, p.belief.known[at(actor)], p.memory, *played});
      }
    }
    apply_record_event(p, event);
  }
  return out;
}

Grade grade_of(double loss, double error, bool played_is_best, const GradeThresholds& t) noexcept {
  if (played_is_best) return Grade::kBest;
  if (loss < t.z * error || loss < t.inaccuracy) return Grade::kGood;
  if (loss < t.mistake) return Grade::kInaccuracy;
  if (loss < t.blunder) return Grade::kMistake;
  return Grade::kBlunder;
}

MoveReview review_decision(const ReviewDecision& d, const ReviewConfig& config, std::span<const ZooBot> pool) {
  const GoldenLabel label = label_position(d.game, d.seat, d.known, d.memory, d.played, config.label, pool);
  MoveReview r;
  r.state = d.state;
  r.seat = d.seat;
  r.turn = d.game.turn_number;
  r.played = d.played;
  r.candidates = static_cast<int>(label.moves.size());
  r.playouts = label.playouts;
  if (label.best < 0 || label.played < 0) {
    r.best = d.played;
    return r;
  }
  const GoldenMove& best = label.moves[at(label.best)];
  const GoldenMove& played = label.moves[at(label.played)];
  r.best = best.action;
  r.best_value = golden_value(best, d.seat);
  r.played_value = golden_value(played, d.seat);
  const bool same = label.best == label.played;
  if (!same) {
    r.loss = r.best_value - r.played_value;
    r.error = paired_error(best.confirm_won, played.confirm_won);
    r.error_unpaired = std::sqrt(binomial_variance(best.confirm, d.seat) + binomial_variance(played.confirm, d.seat));
  }
  r.grade = grade_of(r.loss, r.error, same, config.thresholds);
  r.within_noise = !same && r.loss < config.thresholds.z * r.error;
  return r;
}

json review_json(const MoveReview& r) {
  return {{"state", r.state},
          {"seat", r.seat},
          {"turn", r.turn},
          {"played", action_json(r.played)},
          {"best", action_json(r.best)},
          {"played_value", r.played_value},
          {"best_value", r.best_value},
          {"loss", r.loss},
          {"error", r.error},
          {"error_unpaired", r.error_unpaired},
          {"grade", grade_name(r.grade)},
          {"within_noise", r.within_noise},
          {"candidates", r.candidates},
          {"playouts", r.playouts}};
}

ReviewSummary summarize(std::span<const MoveReview> reviews) {
  ReviewSummary s;
  double total = 0;
  for (const MoveReview& r : reviews) {
    ++s.decisions;
    ++s.grades[static_cast<std::size_t>(r.grade)];
    total += std::max(r.loss, 0.0);
  }
  if (s.decisions > 0) s.mean_loss = total / s.decisions;
  return s;
}

}  // namespace cae
