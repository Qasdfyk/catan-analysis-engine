#pragma once

// Random games: a random standard board, and a random but legal player.
//
// The player is a port of catan-project's random player
// (backend/app/services/random_player.py), decision for decision: it
// prefers moves that score, so its games reach a winner, and it trades with
// the bank and with other players now and then. It is the yardstick for
// "games per second" (tools/selfplay.cpp, docs/performance.md): the Python
// engine runs the same policy, so the two numbers compare the engines, not
// the players. The same seed does not give the same game in both, because
// the two use different random generators.
//
// Later, simulations (step 10) start from a real position and play it out
// with bots from the zoo; this player is the simplest such bot.

#include "catan/game.hpp"
#include "catan/rng.hpp"

#include <array>

namespace cae {

// A board like Board.create_standard_game(): 4 wood, 4 sheep, 4 wheat,
// 3 brick, 3 ore and a desert anywhere; the 18 number tokens shuffled until
// no two red numbers (6, 8) touch; the 9 ports shuffled along the coast at
// the fixed gaps of the printed frame.
[[nodiscard]] Board random_board(Rng& rng);

// The 25 development cards (14 knights, 5 victory points, 2 each of road
// building, year of plenty and monopoly) in a random order.
[[nodiscard]] std::array<DevCard, kDevDeckSize> random_dev_deck(Rng& rng);

// A new game on a random board with a random first seat.
[[nodiscard]] Game random_game(Rng& rng, int players = 4);

// One random legal action of whoever must act now: the seat that owes a
// discard, otherwise the current seat. Chance (dice, the stolen card) is
// drawn from `rng` too. Returns the engine's answer: always kNone, since the
// player only picks legal moves (kGameOver once the game has ended).
Error play_random_step(Game& game, Rng& rng);

struct PlayoutResult {
  int winner = -1;  // -1 when max_actions ran out first
  int actions = 0;  // calls of play_random_step
  int refused = 0;  // steps the engine refused; 0 unless something is broken
};

// Plays random steps until the game ends or max_actions steps have run.
// The limit is the same as in catan-project's simulate().
PlayoutResult play_out(Game& game, Rng& rng, int max_actions = 40'000);

}  // namespace cae
