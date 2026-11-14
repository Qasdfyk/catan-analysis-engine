#include "catan/replay.hpp"

#include "catan/record.hpp"

#include <exception>

namespace cae {

using nlohmann::json;

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

constexpr std::array<const char*, 6> kTerrainNames = {"wood", "brick", "sheep", "wheat", "ore", "desert"};
constexpr std::array<const char*, 6> kPortNames = {"3:1",       "wood_2:1",  "brick_2:1",
                                                   "sheep_2:1", "wheat_2:1", "ore_2:1"};

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
  const Board& b = states_.front().board;
  json hexes = json::array();
  for (int h = 0; h < kHexCount; ++h) {
    hexes.push_back({{"terrain", kTerrainNames[static_cast<std::size_t>(b.terrain[at(h)])]},
                     {"number", b.number[at(h)]}});
  }
  json ports = json::array();
  for (int p = 0; p < b.port_count; ++p) {
    json nodes = json::array();
    for_each_node(b.ports[at(p)].nodes, [&](int n) { nodes.push_back(n); });
    ports.push_back({{"type", kPortNames[static_cast<std::size_t>(b.ports[at(p)].type)]}, {"nodes", nodes}});
  }
  return {{"hexes", hexes}, {"ports", ports}};
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
  for (const auto& e : t.edge_nodes) edges.push_back({e[0], e[1]});
  return {{"hexes", hexes}, {"nodes", nodes}, {"edges", edges}};
}

}  // namespace cae
