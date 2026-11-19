#pragma once

// A bot that searches: Information Set Monte Carlo Tree Search over its own
// moves, with the zoo playing everything else.
//
// The zoo bots (zoo.hpp) follow rules. This bot asks, for each move it could
// make, "how often do I win if I do this?", and answers by playing the game
// out many times from there. It searches the decisions that shape a game:
// the setup, its own turn (build, buy, play a card, trade with the bank, end
// the turn, and an offer when the zoo bot would make one), the robber and the
// victim. Answering offers, confirming its own and discarding after a 7 are
// left to its zoo bot.
//
// One iteration:
//  1. Determinize. Draw a full state that agrees with what the seat knows
//     (belief.hpp): the opponents' hands and development cards and the deck
//     come out differently every iteration. This is what makes it
//     Information Set MCTS (Cowling, Powley and Whitehouse 2012): one tree
//     is shared by all the drawn states, so the statistics of a move average
//     over what the opponents might hold, and the bot never relies on a
//     card it cannot see.
//  2. Select. From the root, while it is still this seat's searched decision,
//     pick a move by PUCT (below) among the moves legal in the drawn state
//     and play it. A roll or a steal draws its outcome on the way, so the
//     tree is open loop: a node is a sequence of the bot's moves, not one
//     state.
//  3. Expand. The first move into a node never visited ends the walk.
//  4. Play out. Zoo bots play the rest of the game: the seat's own zoo bot
//     for it, a bot from `opponents` (the MAP-Elites, or hard) for each other
//     seat.
//  5. Back up. 1 for a win of the seat, 0 otherwise, added to every node on
//     the path.
//
// The tree stops where the bot's turn (or robber move) ends; what the others
// do afterwards is in the playout. The root move played is the one visited
// most.
//
// PUCT (as in AlphaZero, Silver et al. 2017): among the legal children pick
// the highest
//     Q + c * P * sqrt(A) / (1 + n)
// where Q is the child's share of wins (the parent's for a child not yet
// visited), n its visits, A how many times it was legal when its parent was
// visited (the availability count of ISMCTS, since a move legal in one drawn
// state need not be in another) and P its prior. The prior is where
// knowledge enters: the zoo bot's own move gets `zoo_prior` and the other
// moves share the rest. In step 16 of the plan a policy network takes the
// zoo bot's place.

#include "catan/arena.hpp"
#include "catan/belief.hpp"
#include "catan/game.hpp"
#include "catan/zoo.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cae {

struct MctsSettings {
  int iterations = 400;  // playouts per searched decision
  double exploration = 1.0;  // c of PUCT (docs/mcts.md: tuned against the hard bot at 400 playouts)
  double zoo_prior = 0.5;    // prior of the zoo bot's move; the others share the rest
  // Search the true state instead of drawn ones: what seeing every card
  // would be worth. Never in a real game.
  bool peek = false;
  bool trades = true;  // false: no offers to players, decline every offer
};

// One move at the root and its statistics.
struct MctsMove {
  Action action;
  double prior = 0;
  int visits = 0;
  double wins = 0;
  int available = 0;  // iterations in which it was legal
};

struct MctsSearch {
  std::vector<MctsMove> root;  // in the order the moves were first seen
  Action best;
  int iterations = 0;
  int nodes = 0;
};

// Whether the bot searches the move `seat` makes now, rather than leave it to
// its zoo bot.
[[nodiscard]] bool searched_decision(const Game& game, int seat);

// The moves the search chooses among for `seat` now (empty when the decision
// is not searched). `zoo_move` (the zoo bot's choice) is added if it is not
// one of them, which is how an offer to players gets in.
[[nodiscard]] std::vector<Action> search_candidates(const Game& game, int seat,
                                                    const std::optional<Action>& zoo_move, bool trades = true);

// Searches the move of `seat` in `game` with `settings.iterations` playouts.
// `known` is the seat's belief (HandBelief::known[seat]), `memory` the table
// memory of the game. Iteration i draws from its own stream (seed, i), so
// the same arguments give the same search.
[[nodiscard]] MctsSearch mcts_search(const Game& game, int seat, const std::array<Cards, kMaxPlayers>& known,
                                     const TableMemory& memory, const ZooBot& self,
                                     std::span<const ZooBot> opponents, const MctsSettings& settings,
                                     std::uint64_t seed);

// Adds the root statistics of `other` (a search of the same position with
// another seed, for instance on another thread or Web Worker) to `into` and
// picks the best move again: root parallelization.
void merge_searches(MctsSearch& into, const MctsSearch& other);

// The move the bot makes: its zoo bot's where nothing is searched or there is
// one choice, else the search's.
[[nodiscard]] std::optional<Action> mcts_decide(const ArenaView& view, const ZooBot& self,
                                                std::span<const ZooBot> opponents, const MctsSettings& settings,
                                                std::uint64_t seed, MctsSearch* out = nullptr);

// The bot as an arena player. Each decision draws from (seed, decision
// number), so a game replays exactly.
[[nodiscard]] ArenaPlayer mcts_player(const ZooBot& self, std::vector<ZooBot> opponents, MctsSettings settings,
                                      std::uint64_t seed);

// --- Searching the decisions of a recorded game -----------------------------

// A decision of a recorded game the bot would search: the state before it,
// whose move it was, and what that seat knew then.
struct RecordDecision {
  int state = 0;
  int seat = 0;
  std::array<Cards, kMaxPlayers> known{};  // HandBelief::known[seat] at that state
  TableMemory memory;
};

// Every searched decision of the game whose states (ReplayLog: state i after
// event i - 1) are `states`, in order.
[[nodiscard]] std::vector<RecordDecision> record_decisions(std::span<const Game> states);

// `count` of them spread evenly over the game (all when there are fewer).
[[nodiscard]] std::vector<RecordDecision> spread_decisions(std::span<const Game> states, int count);

// The search of a decision as `workers` independent searches of `iterations`
// each, seeds (seed, w), merged: what that many threads or Web Workers
// compute together, and the same result on one.
[[nodiscard]] MctsSearch search_part(const Game& state, const RecordDecision& d, std::span<const ZooBot> opponents,
                                     const MctsSettings& settings, std::uint64_t seed, int worker);

// A move in words: "build_road 34", "trade_with_bank wood ore".
[[nodiscard]] std::string action_text(const Action& a);

}  // namespace cae
