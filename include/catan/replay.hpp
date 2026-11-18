#pragma once

// A finished game held as one state per event, so a viewer can jump to any
// move and back without replaying from the start each time.
//
// This is what the WebAssembly module (wasm/bindings.cpp) hands to the web
// page, but it is plain C++ with no Emscripten in it, so the native tests
// cover it too.

#include "catan/game.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace cae {

class ReplayLog {
 public:
  // Parses a record and applies every event. Never throws: a record that
  // cannot start, or an event the rules refuse, ends up in error() and the
  // states stop at the last good one, so the page can still show how far the
  // game got.
  [[nodiscard]] static ReplayLog from_text(std::string_view record_text);

  // Events in the record, whether or not they all applied.
  [[nodiscard]] int event_count() const noexcept { return event_count_; }
  // States that exist: state 0 is before the first event, state i after
  // event i - 1. Equals event_count() + 1 when nothing was refused.
  [[nodiscard]] int state_count() const noexcept { return static_cast<int>(states_.size()); }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  [[nodiscard]] const Game& state(int i) const { return states_.at(static_cast<std::size_t>(i)); }
  [[nodiscard]] const std::vector<Game>& states() const noexcept { return states_; }
  // snapshot() of state i (record.hpp) plus what a viewer needs on top:
  // cards left in the bank.
  [[nodiscard]] nlohmann::json state_json(int i) const;
  // Terrain, numbers and ports by index, the part of the board that never
  // changes during a game.
  [[nodiscard]] nlohmann::json board_json() const;

 private:
  int event_count_ = 0;
  std::vector<Game> states_;
  std::string error_;
};

// The positions behind the indices of state_json() and board_json(): the
// cube coordinates of every hex, and every corner and side as (hex,
// direction) in its canonical spelling, the form game records and
// catan-project's board use. Sides also list their two corners.
[[nodiscard]] nlohmann::json topology_json();

}  // namespace cae
