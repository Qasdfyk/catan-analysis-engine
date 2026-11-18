#pragma once

// Zoo bots and MAP-Elites archives as JSON (data/elites.json, written by
// catan_map_elites and read by catan_arena and catan_winrate).

#include "catan/map_elites.hpp"
#include "catan/zoo.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace cae {

[[nodiscard]] nlohmann::json zoo_bot_json(const ZooBot& bot);
// Throws nlohmann::json::exception on a malformed bot.
[[nodiscard]] ZooBot zoo_bot_from_json(const nlohmann::json& j);

[[nodiscard]] nlohmann::json evaluation_json(const Evaluation& e);
[[nodiscard]] Evaluation evaluation_from_json(const nlohmann::json& j);

// The elites of a file in the order it lists them (best final score first).
// Throws std::runtime_error if the file cannot be read.
[[nodiscard]] std::vector<ZooBot> load_elites(const std::string& path);

// A bot by the name the tools take:
//   hard                         the hard level
//   ARCHETYPE/PERSONALITY[:SEED] a zoo bot ("zoo:" in front is allowed)
//   elite:N                      elite N of the archive at `elites_path`
// Any of them may end in "+leader=X" to set the Robber's leader focus.
// Throws std::runtime_error on an unknown name.
[[nodiscard]] ZooBot zoo_bot_named(std::string name, const std::string& elites_path = "data/elites.json");

}  // namespace cae
