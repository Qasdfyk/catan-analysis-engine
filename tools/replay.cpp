// catan_replay: replays a game record through the C++ rules and prints the
// board, each player's start (production after the setup) and the result.
//
//   build/release/tools/catan_replay game.json
//
// Exit status 0 when every event was accepted and the final points match the
// record's result, 1 otherwise.

#include "catan/record.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using cae::Game;
using nlohmann::json;

constexpr const char* kTerrainShort[] = {"WOOD", "BRCK", "SHEP", "WHET", "ORE ", "DESR"};

// Chance of a number in 36 rolls: 6 and 8 come up 5 times, 2 and 12 once.
int pips(int number) { return number == 0 ? 0 : 6 - std::abs(7 - number); }

void print_board(const Game& g) {
  std::printf("Board (robber on the desert):\n\n");
  for (int r = -2; r <= 2; ++r) {
    std::printf("%*s", 5 * std::abs(r), "");
    for (int q = -2; q <= 2; ++q) {
      const int h = cae::hex_at(q, r);
      if (h < 0) continue;  // sea
      const auto i = static_cast<std::size_t>(h);
      const int n = g.board.number[i];
      if (n == 0) {
        std::printf(" %s     ", kTerrainShort[static_cast<int>(g.board.terrain[i])]);
      } else {
        std::printf(" %s %2d%s ", kTerrainShort[static_cast<int>(g.board.terrain[i])], n,
                    (n == 6 || n == 8) ? "!" : " ");
      }
    }
    std::printf("\n");
  }
  std::printf("\n");
}

// Expected cards per 36 rolls from each player's buildings, per resource.
void print_start(const Game& g, const json& players) {
  std::printf("After the setup: expected cards per 36 rolls (pips)\n");
  std::printf("  %-10s %5s %5s %5s %5s %5s %6s\n", "player", "wood", "brick", "sheep", "wheat", "ore", "total");
  for (int s = 0; s < g.player_count; ++s) {
    std::array<int, cae::kResourceCount> per{};
    const auto& p = g.players[static_cast<std::size_t>(s)];
    for (int h = 0; h < cae::kHexCount; ++h) {
      const auto i = static_cast<std::size_t>(h);
      if (g.board.terrain[i] == cae::Terrain::kDesert) continue;
      const int touching = cae::count(p.settlements & cae::kTopology.hex_node_mask[i]) +
                           2 * cae::count(p.cities & cae::kTopology.hex_node_mask[i]);
      per[static_cast<std::size_t>(g.board.terrain[i])] += touching * pips(g.board.number[i]);
    }
    int total = 0;
    for (int n : per) total += n;
    std::printf("  %-10s %5d %5d %5d %5d %5d %6d\n",
                players[static_cast<std::size_t>(s)].value("name", "?").c_str(), per[0], per[1], per[2],
                per[3], per[4], total);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s RECORD.json\n", argv[0]);
    return 2;
  }
  try {
    const auto t0 = std::chrono::steady_clock::now();
    const json record = cae::read_record_file(argv[1]);
    const auto t1 = std::chrono::steady_clock::now();
    const json& events = record.at("events");
    const json& players = record.at("players");

    Game g = cae::game_from_record(record);
    print_board(g);

    const int setup_events = 4 * g.player_count;  // settlement + road, twice each
    for (std::size_t i = 0; i < events.size(); ++i) {
      const cae::Error e = cae::apply_event(g, events[i]);
      if (e != cae::Error::kNone) {
        std::printf("event %zu (%s) refused: %s\n", i, events[i].value("action", "?").c_str(),
                    std::string(cae::error_name(e)).c_str());
        return 1;
      }
      if (static_cast<int>(i) + 1 == setup_events) print_start(g, players);
    }

    // Time the replay alone: the JSON is already parsed, so this is the rules.
    constexpr int kRuns = 200;
    const auto t2 = std::chrono::steady_clock::now();
    int checksum = 0;
    for (int run = 0; run < kRuns; ++run) {
      Game again = cae::game_from_record(record);
      for (const json& event : events) checksum += static_cast<int>(cae::apply_event(again, event));
      checksum += again.turn_number;
    }
    const auto t3 = std::chrono::steady_clock::now();
    const auto us = [](auto d) { return std::chrono::duration<double, std::micro>(d).count(); };

    std::printf("Result after %zu events, %d turns:\n", events.size(), g.turn_number);
    bool agrees = true;
    const json& result = record.value("result", json(nullptr));
    for (int s = 0; s < g.player_count; ++s) {
      const int points = g.visible_points(s) + g.hidden_points(s);
      const bool won = g.winner == s;
      std::printf("  seat %d %-10s %2d points%s\n", s,
                  players[static_cast<std::size_t>(s)].value("name", "?").c_str(), points,
                  won ? "  <- winner" : "");
      if (result.is_object() && result.at("victory_points").at(static_cast<std::size_t>(s)) != points) {
        agrees = false;
      }
    }
    if (result.is_object()) {
      agrees = agrees && result.at("winner_seat") == g.winner && result.at("turns") == g.turn_number;
      std::printf("Record's result: %s\n", agrees ? "matches" : "DIFFERS");
    }
    std::printf("\nRead and parse the file: %8.1f us\n", us(t1 - t0));
    std::printf("Replay the game (rules):  %8.1f us  (average of %d, checksum %d)\n",
                us(t3 - t2) / kRuns, kRuns, checksum % 10);
    return agrees ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
