#pragma once

// The searching bot (mcts.hpp) as a player of catan-project: the "expert"
// level of the game.
//
// The platform's bots are Go programs that see the game the way a person
// does. The search needs more: what the seat can know of the other hands
// (belief.hpp), which only the history of the game tells, and the table
// memory of the zoo bot (zoo.hpp). Both come from the record the platform
// keeps of every running game (docs/game-record.md in catan-project). So a
// Go bot asks, on its move, with the record so far and its seat; the engine
// replays the record, follows what that seat saw, and answers with a move in
// the platform's form (record.hpp, event_from_action).
//
// The record holds more than the seat may know: the card a steal took
// between two others, the development cards drawn. The search reads only
// HandBelief::known[seat], and the tests of belief.hpp and mcts.hpp check
// that two states differing only in what the seat cannot see give the same
// search. tools/expert.cpp serves this over stdin and stdout.

#include "catan/arena.hpp"
#include "catan/belief.hpp"
#include "catan/game.hpp"
#include "catan/mcts.hpp"
#include "catan/zoo.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cae {

// A running game rebuilt from its record, with what every seat knows and
// the table memory, kept exactly as the arena keeps them while it plays.
struct TablePosition {
  Game game;
  HandBelief belief;
  TableMemory memory;
  std::vector<Built> built;
  int events = 0;
};

// Replays every event of `record`. Throws RecordError when the record cannot
// start or the rules refuse an event (a record of the platform never holds a
// refused move).
[[nodiscard]] TablePosition table_from_record(const nlohmann::json& record);

struct ExpertSettings {
  MctsSettings search;  // search.iterations: playouts in all, over every thread
  int threads = 4;      // root parallelization (merge_searches)
  // Time for one searched move; zero means none, and then the move depends
  // only on the record and the seed.
  std::chrono::milliseconds time_limit{1000};
};

struct ExpertMove {
  // Nothing when the seat has no move now: not its turn, nothing to answer,
  // or its own offer is still waiting for answers.
  std::optional<Action> action;
  bool searched = false;  // false: the hard bot's move, without a search
  MctsSearch search;      // the merged search, when searched
  double milliseconds = 0;
};

// The move of `seat` in `position`. The seat's own bot is the hard bot (in
// what it does not search and in its playouts), the others are drawn from
// `opponents` (empty: hard). The streams depend on `seed`, the number of
// events and the seat, so asking twice about the same moment without a
// time limit gives the same move.
[[nodiscard]] ExpertMove expert_move(const TablePosition& position, int seat, std::span<const ZooBot> opponents,
                                     const ExpertSettings& settings, std::uint64_t seed);

}  // namespace cae
