#pragma once

// Golden positions (set B of docs/benchmarks.md): decisions from real games
// of the platform, each labelled once by a deep Monte Carlo analysis and
// frozen. Step 13 (grading moves) and later the network are checked against
// them: the chance to win of a position, and how much each move loses
// against the best one.
//
// A label is the chance to win after each move the searching bot would
// consider (mcts.hpp, search_candidates), measured the way the setup review
// measures corners (setup_review.hpp):
//
//   halving   every move gets `first_playouts` playouts, the better half
//             twice as many more, and so on until `finalists` are left
//   confirm   every finalist and the move played get `confirm_playouts`
//             fresh playouts; their values come from these alone, so the
//             best move's value is not inflated by the noise that chose it
//
// One playout of a move: draw the hidden cards from what the seat knows
// (determinize, belief.hpp), make the move, and let zoo bots play the game
// out: the hard bot for the seat (it finishes the turn), a bot drawn from
// `pool` for each opponent. Playout i draws the same hidden cards and the
// same chance for every move, so two moves are compared on the same
// futures as far as the moves let them stay alike.

#include "catan/belief.hpp"
#include "catan/game.hpp"
#include "catan/winrate.hpp"
#include "catan/zoo.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cae {

struct GoldenConfig {
  int first_playouts = 128;  // per move in the first halving round; doubles every round
  int finalists = 3;         // halving stops at this many (plus the move played)
  int confirm_playouts = 2400;
  std::uint64_t seed = 1;
};

struct GoldenMove {
  Action action;
  WinCounts halving;
  WinCounts confirm;  // finalists and the move played only
  bool finalist = false;
};

struct GoldenLabel {
  std::vector<GoldenMove> moves;  // in the order of search_candidates
  int best = -1;    // the finalist ranked first by the halving
  int played = -1;  // the move the player made, -1 if none or not a candidate
  long long playouts = 0;
};

// Labels the decision of `seat` in `truth`, seen with `known`
// (HandBelief::known[seat]) and the table memory. `played` is the move made
// in the game; it is added to the moves when the search would not consider
// it (an offer the hard bot would not make), and it always gets the
// confirmation playouts.
[[nodiscard]] GoldenLabel label_position(const Game& truth, int seat, const std::array<Cards, kMaxPlayers>& known,
                                         const TableMemory& memory, const std::optional<Action>& played,
                                         const GoldenConfig& config, std::span<const ZooBot> pool);

// The value of a move: its confirmation if it has one, else the halving.
[[nodiscard]] double golden_value(const GoldenMove& m, int seat) noexcept;

// The table memory as JSON and back, for positions stored without a game.
[[nodiscard]] nlohmann::json memory_json(const TableMemory& memory);
[[nodiscard]] TableMemory memory_from_json(const nlohmann::json& j);

}  // namespace cae
