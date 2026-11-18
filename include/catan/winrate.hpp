#pragma once

// Chances to win, by Monte Carlo, and where along a game to measure them.
//
// From a position, zoo bots (zoo.hpp) play the game out to the end many
// times; the share of playouts a seat wins is its chance to win from there.
//
// What a playout keeps from the position and what it draws anew:
//   kept   the board, buildings, roads, robber, every hand, development
//          cards already held, points, awards, whose turn it is
//   drawn  the dice, stolen cards, the order of the undrawn development
//          cards (nobody knows it), and the players: every seat is a new
//          random zoo bot each playout, unless the caller says which bot sat
//          where (for games the zoo itself played)
// Hands and held cards are those of the record, so this is a spectator's
// view. A player's own view, with hidden cards drawn from what they could
// know, comes with the move review (step 13 of the plan).
//
// Luck and decisions. Every turn has two points: just before the dice are
// rolled and just after (the roll and the cards it paid, nothing more). The
// change of a seat's chance across the roll is luck; the change from after
// one roll to before the next is decisions: building, trading, the robber
// after a 7, cards played. Summed over the game, luck + decisions = how the
// game ended minus the chance at the start, exactly.
//
// Playout i of any position draws from its own stream (seed, i), so
// playouts can be split into chunks, run on different threads or Web
// Workers, and added up to the very same result.
//
// Noise. Each point is an estimate with its own error, and the split adds
// up some 150 differences of them, so the errors add up too: with 400
// playouts per point the luck of a seat over a whole game moves by 10-20
// percentage points from one seed to another (docs/win-chances.md). Fixing
// the dice of each turn across positions (common random numbers) did not
// help: after a different roll the bots hold other cards and the games
// part ways within a turn or two. A precise split needs a cheap value
// function to average over all 36 rolls, which is what the value network of
// steps 14-16 is for (GNU Backgammon measures luck the same way).

#include "catan/game.hpp"
#include "catan/zoo.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cae {

enum class PointKind : std::uint8_t {
  kStart,       // before the first settlement
  kBeforeRoll,  // a turn after setup, just before its roll
  kAfterRoll,   // just after that roll
  kEnd,         // the last state of the record
};

struct ChancePoint {
  int state = 0;  // index into the states of the game (ReplayLog: state i is after event i - 1)
  PointKind kind = PointKind::kStart;
  int turn = 0;   // Game::turn_number at that state
  int seat = -1;  // whose turn it is
};

// The points to estimate along a game, in order. `states` holds every state
// of the game, the first before any event (ReplayLog keeps them like that).
[[nodiscard]] std::vector<ChancePoint> chance_points(std::span<const Game> states);

// Wins of each seat over some playouts. Counts, not shares, so chunks add up.
struct WinCounts {
  std::array<int, kMaxPlayers> wins{};
  int unfinished = 0;  // playouts that hit the action limit without a winner
  int playouts = 0;

  WinCounts& operator+=(const WinCounts& o) noexcept;
  [[nodiscard]] double share(int seat) const noexcept {
    return playouts > 0 ? static_cast<double>(wins[static_cast<std::size_t>(seat)]) / playouts : 0.0;
  }
};

// Who sits where in the playouts: nothing = a random zoo bot per seat and
// playout, as for a game between people.
using SeatBots = std::array<std::optional<ZooBot>, kMaxPlayers>;

// Playouts first .. first + count - 1 from `start`. A finished game counts
// every playout for its winner without playing any.
[[nodiscard]] WinCounts play_out_chances(const Game& start, int first, int count, std::uint64_t seed,
                                         const SeatBots& bots = {});

}  // namespace cae
