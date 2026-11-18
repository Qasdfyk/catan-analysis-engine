#include "catan/replay.hpp"

#include "catan/record.hpp"

#include <exception>

namespace cae {

using nlohmann::json;

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }


}  // namespace

ReplayLog ReplayLog::from_text(std::string_view record_text) {
  ReplayLog log;
  // Everything below can throw (bad JSON, a broken header, a field of the
  // wrong type deep in an event). The page gets a message, not a crash.
  try {
    const json record = json::parse(record_text);
    const json& events = record.at("events");
    log.event_count_ = static_cast<int>(events.size());
    // One Game is under 1 KB with no heap inside, so keeping every state of a
    // game of a few thousand events costs a few MB and makes seeking O(1).
    log.states_.reserve(events.size() + 1);
    log.states_.push_back(game_from_record(record));
    for (std::size_t i = 0; i < events.size(); ++i) {
      Game next = log.states_.back();
      const Error e = apply_event(next, events[i]);
      if (e != Error::kNone) {
        log.error_ = "event " + std::to_string(i) + " (" + events[i].value("action", "?") +
                     ") refused: " + std::string(error_name(e));
        break;
      }
      log.states_.push_back(next);
    }
  } catch (const std::exception& e) {
    log.error_ = e.what();
  }
  return log;
}

json ReplayLog::state_json(int i) const {
  const Game& g = state(i);
  json out = snapshot(g);
  json bank = json::array();
  for (int r = 0; r < kResourceCount; ++r) bank.push_back(g.bank_count(static_cast<Resource>(r)));
  out["bank"] = bank;
  out["dice"] = g.dice_roll > 0 ? json{g.dice[0], g.dice[1]} : json(nullptr);
  return out;
}

json ReplayLog::board_json() const {
  if (states_.empty()) return nullptr;
  return cae::board_json(states_.front().board);
}

json topology_json() {
  const Topology& t = kTopology;
  json hexes = json::array();
  for (const Cube& c : t.hex_cube) hexes.push_back({{"q", c.q}, {"r", c.r}});
  json nodes = json::array();
  for (const Spelling& s : t.node_spelling) {
    nodes.push_back({{"q", s.hex.q}, {"r", s.hex.r}, {"direction", s.direction}});
  }
  json edges = json::array();
  for (int e = 0; e < kEdgeCount; ++e) {
    const Spelling& s = t.edge_spelling[at(e)];
    const auto& ends = t.edge_nodes[at(e)];
    edges.push_back({{"q", s.hex.q}, {"r", s.hex.r}, {"direction", s.direction}, {"nodes", {ends[0], ends[1]}}});
  }
  return {{"hexes", hexes}, {"nodes", nodes}, {"edges", edges}};
}

}  // namespace cae
