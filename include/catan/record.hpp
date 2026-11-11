#pragma once

// Game records (docs/game-record.md in catan-project) read into the engine.
//
// This is a separate library from the rules (catan_record, not catan) so the
// rules engine stays free of the JSON dependency; the WebAssembly build and
// the simulations need only the rules.

#include "catan/game.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace cae {

// A record the engine cannot start from: wrong format or version, a board
// that is not the standard one, a robber that does not start on the desert.
class RecordError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// The game as it is before the first event. Throws RecordError.
[[nodiscard]] Game game_from_record(const nlohmann::json& record);

// Applies one event (`action`, `payload`, `outcome`) through the rules.
// A payload that does not parse is refused with Error::kBadArgument, as the
// platform refuses it; recorded chance that cannot happen gives kBadOutcome.
[[nodiscard]] Error apply_event(Game& game, const nlohmann::json& event);

// The whole state as JSON, with positions as indices (node 0..53, edge
// 0..71, hex 0..18). tools/difftest.py builds the same document from the
// Python game and compares the two.
[[nodiscard]] nlohmann::json snapshot(const Game& game);

// The legal moves of one seat, in the same index form.
[[nodiscard]] nlohmann::json legal_moves(const Game& game, int seat);

// Reads a record file: plain JSON (downloaded from the game) only.
[[nodiscard]] nlohmann::json read_record_file(const std::string& path);

}  // namespace cae
