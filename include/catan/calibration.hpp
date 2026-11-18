#pragma once

// Calibration of win chances: when an estimate says 30%, does that player win
// about 30% of the time? Shared by catan_winrate (Monte Carlo on recorded
// games) and catan_validate (set A, the baseline model).
//
// Predictions come grouped by game. Positions of one game share its winner,
// so they are not independent; the 95% intervals come from a bootstrap over
// games (draw as many games as there are, with replacement, recompute, take
// the 2.5th and 97.5th percentiles), not over positions, which would make
// them too narrow.
//
// Brier score: the mean of (chance - outcome)^2 over every seat of every
// position, outcome 1 for the winner and 0 for the others. Lower is better;
// always saying 1/n scores (1/n)(1 - 1/n), 0.1875 with four players.

#include "catan/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace cae {

struct Prediction {
  double chance = 0;
  int won = 0;
};

struct Calibration {
  static constexpr int kBuckets = 10;
  std::array<long, kBuckets> said{};
  std::array<long, kBuckets> won{};
  std::array<double, kBuckets> sum_chance{};
  double brier = 0;
  long count = 0;

  void add(const Prediction& p) {
    const int b = std::min(kBuckets - 1, static_cast<int>(p.chance * kBuckets));
    ++said[static_cast<std::size_t>(b)];
    won[static_cast<std::size_t>(b)] += p.won;
    sum_chance[static_cast<std::size_t>(b)] += p.chance;
    brier += (p.chance - p.won) * (p.chance - p.won);
    ++count;
  }
  [[nodiscard]] double won_share(int b) const {
    const auto i = static_cast<std::size_t>(b);
    return said[i] > 0 ? static_cast<double>(won[i]) / static_cast<double>(said[i]) : NAN;
  }
  [[nodiscard]] double brier_score() const { return count > 0 ? brier / static_cast<double>(count) : NAN; }
};

inline double percentile(std::vector<double> v, double q) {
  std::erase_if(v, [](double x) { return std::isnan(x); });
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))];
}

struct CalibrationReport {
  Calibration all;
  std::array<double, Calibration::kBuckets> lo{};
  std::array<double, Calibration::kBuckets> hi{};
  double brier_lo = 0;
  double brier_hi = 0;
};

// Every prediction, plus bootstrap intervals over games.
inline CalibrationReport calibrate(const std::vector<std::vector<Prediction>>& by_game, int bootstrap,
                                   std::uint64_t seed) {
  CalibrationReport r;
  for (const auto& preds : by_game) {
    for (const auto& p : preds) r.all.add(p);
  }
  std::array<std::vector<double>, Calibration::kBuckets> boot_won;
  std::vector<double> boot_brier;
  Rng rng(seed);
  const int n = static_cast<int>(by_game.size());
  for (int b = 0; b < bootstrap && n > 0; ++b) {
    Calibration c;
    for (int k = 0; k < n; ++k) {
      for (const auto& p : by_game[static_cast<std::size_t>(rng.below(n))]) c.add(p);
    }
    for (int k = 0; k < Calibration::kBuckets; ++k) boot_won[static_cast<std::size_t>(k)].push_back(c.won_share(k));
    boot_brier.push_back(c.brier_score());
  }
  for (int k = 0; k < Calibration::kBuckets; ++k) {
    r.lo[static_cast<std::size_t>(k)] = percentile(boot_won[static_cast<std::size_t>(k)], 0.025);
    r.hi[static_cast<std::size_t>(k)] = percentile(boot_won[static_cast<std::size_t>(k)], 0.975);
  }
  r.brier_lo = percentile(boot_brier, 0.025);
  r.brier_hi = percentile(boot_brier, 0.975);
  return r;
}

inline void print_calibration(const CalibrationReport& r, int players) {
  std::printf("calibration: when it said ..., the player won ... (95%% interval, bootstrap over games)\n");
  std::printf("%10s %8s %8s %17s %7s\n", "said", "mean", "won", "interval", "count");
  for (int k = 0; k < Calibration::kBuckets; ++k) {
    const auto i = static_cast<std::size_t>(k);
    if (r.all.said[i] == 0) continue;
    const double mean = r.all.sum_chance[i] / static_cast<double>(r.all.said[i]);
    const bool inside = mean >= r.lo[i] && mean <= r.hi[i];
    std::printf("%3d-%3d%%  %7.1f%% %7.1f%%   %5.1f%% .. %5.1f%% %7ld%s\n", k * 10, k * 10 + 10, 100 * mean,
                100 * r.all.won_share(k), 100 * r.lo[i], 100 * r.hi[i], r.all.said[i], inside ? "" : "  <- outside");
  }
  const double base = (1.0 / players) * (1 - 1.0 / players);
  std::printf("\nBrier score %.4f (95%% interval %.4f .. %.4f); always saying 1/%d scores %.4f\n",
              r.all.brier_score(), r.brier_lo, r.brier_hi, players, base);
}

}  // namespace cae
