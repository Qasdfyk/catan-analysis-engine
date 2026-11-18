#pragma once

// Game records (docs/game-record.md in catan-project) read into the engine.
//
// This is a separate library from the rules (catan_record, not catan) so the
// rules engine stays free of the JSON dependency; the WebAssembly build and
// the simulations need only the rules.

#include "catan/game.hpp"
#include "catan/zoo.hpp"

#include <nlohmann/json.hpp>

#include <optional>
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

// The move of an event, without its outcome (the dice, the stolen card):
// what a bot chose. Nothing for an event that does not parse.
[[nodiscard]] std::optional<Action> action_from_event(const nlohmann::json& event);

// The bot that played a seat of a record: "hard", or "zoo" rebuilt from the
// shaken weights and trading in its bot_style (so no Go random generator is
// needed). Nothing for people and the other levels.
[[nodiscard]] std::optional<ZooBot> zoo_bot_from_record(const nlohmann::json& player);

// --- Positions in index form ----------------------------------------------
//
// A position written down without its game: the board and a snapshot(). The
// validation set A (docs/arena.md) stores positions like this, and the
// arena hands them to bots that run in another process.

// Terrain, numbers and ports by index: {"hexes": [{"terrain", "number"}],
// "ports": [{"type", "nodes": [a, b]}]}.
[[nodiscard]] nlohmann::json board_json(const Board& board);
[[nodiscard]] Board board_from_json(const nlohmann::json& board);

// The inverse of snapshot(): snapshot(game_from_snapshot(b, snapshot(g)))
// == snapshot(g), and the game plays on exactly as g would (tests check
// both). Settings are the defaults. Throws RecordError.
[[nodiscard]] Game game_from_snapshot(const nlohmann::json& board, const nlohmann::json& state);

// One move in index form: {"kind": "build_road", "target": 17, ...}, with only
// the fields the kind uses. action_from_json() returns nothing for anything
// it cannot read.
[[nodiscard]] nlohmann::json action_json(const Action& action);
[[nodiscard]] std::optional<Action> action_from_json(const nlohmann::json& action);

// Reads a record file: plain JSON (downloaded from the game) only.
[[nodiscard]] nlohmann::json read_record_file(const std::string& path);

}  // namespace cae
