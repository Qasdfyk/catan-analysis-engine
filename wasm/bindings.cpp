// The engine as a WebAssembly module for the web viewer (web/).
//
// Embind exports a small API to JavaScript. Everything crosses the boundary
// as a string of JSON: the page parses it with JSON.parse, which is fast and
// keeps this file free of per-field glue. Positions are indices (hex 0..18,
// corner 0..53, side 0..71) and the page draws them from topologyJson().
//
//   const engine = await createEngine();
//   const replay = new engine.Replay(recordText);
//   replay.error();            // "" when every event applied
//   JSON.parse(replay.stateJson(42));
//   replay.delete();           // C++ memory is not garbage collected
//
// selfplay() runs the random player of catan_selfplay, single-threaded, to
// compare the engine in the browser with the native build; zooSelfplay()
// runs zoo bots, like `catan_selfplay --bots zoo`.

#include "catan/random_player.hpp"
#include "catan/replay.hpp"
#include "catan/zoo.hpp"

#include <emscripten/bind.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace {

class Replay {
 public:
  explicit Replay(const std::string& record_text) : log_(cae::ReplayLog::from_text(record_text)) {}

  int eventCount() const { return log_.event_count(); }
  int stateCount() const { return log_.state_count(); }
  std::string error() const { return log_.error(); }
  // An index out of range gives "null", not an exception across the boundary.
  std::string stateJson(int i) const {
    if (i < 0 || i >= log_.state_count()) return "null";
    return log_.state_json(i).dump();
  }
  std::string boardJson() const { return log_.board_json().dump(); }

 private:
  cae::ReplayLog log_;
};

std::string topologyJson() { return cae::topology_json().dump(); }

// The same games as `catan_selfplay --games N --players P --seed S`, so the
// checksum must match the native one: that proves the WebAssembly build plays
// the same moves, not just that it runs.
std::string play_games(int games, int players, double seed_number, bool zoo) {
  const auto seed = static_cast<std::uint64_t>(seed_number);
  long long actions = 0;
  long long refused = 0;
  std::uint64_t checksum = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < games; ++i) {
    // The same streams and the same order of draws as tools/selfplay.cpp
    cae::Rng rng((seed << 32) ^ static_cast<std::uint64_t>(i));
    cae::PlayoutResult r;
    if (zoo) {
      cae::ZooTable table;
      table.game = cae::random_game(rng, players);
      for (int s = 0; s < players; ++s) table.bots[static_cast<std::size_t>(s)] = cae::random_zoo_bot(rng);
      const cae::ZooPlayout z = cae::play_out_zoo(table, rng);
      r = {z.winner, z.actions, z.fallbacks};
    } else {
      cae::Game game = cae::random_game(rng, players);
      r = cae::play_out(game, rng);
    }
    actions += r.actions;
    refused += r.refused;
    checksum += static_cast<std::uint64_t>(r.actions) * 31 + static_cast<std::uint64_t>(r.winner + 1);
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  // The checksum is a string: a 64-bit integer does not fit a JS number.
  nlohmann::json out = {{"games", games},
                        {"actions", actions},
                        {"refused", refused},
                        {"checksum", std::to_string(checksum)},
                        {"seconds", seconds}};
  return out.dump();
}

std::string selfplay(int games, int players, double seed) { return play_games(games, players, seed, false); }
std::string zooSelfplay(int games, int players, double seed) { return play_games(games, players, seed, true); }

}  // namespace

EMSCRIPTEN_BINDINGS(catan_engine) {
  emscripten::class_<Replay>("Replay")
      .constructor<const std::string&>()
      .function("eventCount", &Replay::eventCount)
      .function("stateCount", &Replay::stateCount)
      .function("error", &Replay::error)
      .function("stateJson", &Replay::stateJson)
      .function("boardJson", &Replay::boardJson);
  emscripten::function("topologyJson", &topologyJson);
  emscripten::function("selfplay", &selfplay);
  emscripten::function("zooSelfplay", &zooSelfplay);
}
