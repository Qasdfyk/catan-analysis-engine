#include "catan/winrate.hpp"

#include <utility>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// Fisher-Yates over the development cards not drawn yet: the record knows
// their order, nobody at the table does.
void shuffle_undrawn(Game& game, Rng& rng) {
  for (int i = game.dev_deck_size - 1; i > game.dev_deck_next; --i) {
    const int j = game.dev_deck_next + rng.below(i - game.dev_deck_next + 1);
    std::swap(game.dev_deck[at(i)], game.dev_deck[at(j)]);
  }
}

}  // namespace

std::vector<ChancePoint> chance_points(std::span<const Game> states) {
  std::vector<ChancePoint> points;
  if (states.empty()) return points;
  const auto point = [&](std::size_t i, PointKind kind) {
    const Game& g = states[i];
    points.push_back({static_cast<int>(i), kind, g.turn_number, g.current});
  };
  point(0, PointKind::kStart);
  // The roll is the one event that takes dice_roll from 0 to a number. A
  // knight played before the roll stays on the decisions side of it.
  for (std::size_t i = 0; i + 1 < states.size(); ++i) {
    if (states[i].dice_roll == 0 && states[i + 1].dice_roll != 0) {
      point(i, PointKind::kBeforeRoll);
      point(i + 1, PointKind::kAfterRoll);
    }
  }
  if (states.size() > 1) point(states.size() - 1, PointKind::kEnd);
  return points;
}

WinCounts& WinCounts::operator+=(const WinCounts& o) noexcept {
  for (std::size_t s = 0; s < wins.size(); ++s) wins[s] += o.wins[s];
  unfinished += o.unfinished;
  playouts += o.playouts;
  return *this;
}

WinCounts play_out_chances(const Game& start, int first, int count, std::uint64_t seed, const SeatBots& bots) {
  WinCounts out;
  out.playouts = count;
  if (start.winner >= 0) {
    out.wins[at(start.winner)] = count;
    return out;
  }
  for (int p = first; p < first + count; ++p) {
    // The stream of playout p, whatever the position, thread or chunk.
    Rng rng(seed * 0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(p));
    ZooTable table;
    table.game = start;
    shuffle_undrawn(table.game, rng);
    for (int s = 0; s < start.player_count; ++s) {
      table.bots[at(s)] = bots[at(s)] ? *bots[at(s)] : random_zoo_bot(rng);
    }
    const ZooPlayout r = play_out_zoo(table, rng);
    if (r.winner < 0) {
      ++out.unfinished;
    } else {
      ++out.wins[at(r.winner)];
    }
  }
  return out;
}

}  // namespace cae
