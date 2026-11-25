// Golden positions: the move played always gets its confirmation, the best
// move is a finalist, a label is reproducible, and a stored position comes
// back the same.

#include "catan/expert.hpp"
#include "catan/golden.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cae {
namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

json read_record(const std::string& name) {
  std::ifstream in(std::string(CATAN_TEST_DATA_DIR) + "/" + name);
  std::stringstream text;
  text << in.rdbuf();
  return json::parse(text.str());
}

struct Decision {
  TablePosition table;
  int seat = 0;
  std::optional<Action> played;
};

std::vector<Decision> decisions(const std::string& name, int count) {
  const json record = read_record(name);
  const ReplayLog log = ReplayLog::from_text(record.dump());
  std::vector<Decision> out;
  for (const RecordDecision& d : spread_decisions(log.states(), count)) {
    json cut = record;
    cut["events"] = json(std::vector<json>(record["events"].begin(), record["events"].begin() + d.state));
    Decision one{table_from_record(cut), d.seat, std::nullopt};
    const json& event = record["events"][at(d.state)];
    if (event["seat"] == d.seat) one.played = action_from_event(event);
    out.push_back(std::move(one));
  }
  return out;
}

GoldenConfig small() {
  GoldenConfig c;
  c.first_playouts = 8;
  c.finalists = 2;
  c.confirm_playouts = 32;
  return c;
}

TEST(Golden, ThePlayedMoveAndTheFinalistsGetTheirConfirmation) {
  int with_played = 0;
  for (const Decision& d : decisions("zoo-game-00088.json", 8)) {
    const GoldenLabel label = label_position(d.table.game, d.seat, d.table.belief.known[at(d.seat)], d.table.memory,
                                             d.played, small(), {});
    ASSERT_FALSE(label.moves.empty());
    ASSERT_GE(label.best, 0);
    EXPECT_TRUE(label.moves[at(label.best)].finalist);
    int finalists = 0;
    for (const GoldenMove& m : label.moves) {
      EXPECT_EQ(m.confirm.playouts, m.finalist ? 32 : 0);
      EXPECT_GE(m.halving.playouts, 8);
      finalists += m.finalist ? 1 : 0;
    }
    if (d.played) {
      ASSERT_GE(label.played, 0);
      EXPECT_EQ(label.moves[at(label.played)].action, *d.played);
      EXPECT_TRUE(label.moves[at(label.played)].finalist);
      ++with_played;
    }
    EXPECT_LE(finalists, 3);  // two finalists and the move played
  }
  EXPECT_GT(with_played, 0);
}

TEST(Golden, ALabelIsReproducible) {
  const Decision d = decisions("hard-game-00030.json", 4).at(2);
  const auto label = [&] {
    return label_position(d.table.game, d.seat, d.table.belief.known[at(d.seat)], d.table.memory, d.played, small(), {});
  };
  const GoldenLabel a = label();
  const GoldenLabel b = label();
  ASSERT_EQ(a.moves.size(), b.moves.size());
  for (std::size_t i = 0; i < a.moves.size(); ++i) {
    EXPECT_EQ(a.moves[i].halving.wins, b.moves[i].halving.wins);
    EXPECT_EQ(a.moves[i].confirm.wins, b.moves[i].confirm.wins);
  }
  EXPECT_EQ(a.best, b.best);
}

// A stored position plays out like the position in the game: the same
// playouts give the same wins (this failed for every position where
// development cards had been bought, until the snapshot kept the drawn part).
TEST(Golden, AStoredPositionComesBackTheSame) {
  for (const Decision& d : decisions("zoo-game-00206.json", 12)) {
    const Game back = game_from_snapshot(board_json(d.table.game.board), snapshot(d.table.game));
    EXPECT_EQ(snapshot(back), snapshot(d.table.game));
    // The snapshot lists only the cards left in the deck; the cards drawn
    // must come back too, or hidden cards are dealt from cards nobody has.
    EXPECT_EQ(back.dev_deck_next, d.table.game.dev_deck_next);
    EXPECT_EQ(played_dev_cards(back), played_dev_cards(d.table.game));
    if (!d.played) continue;
    const auto& known = d.table.belief.known[at(d.seat)];
    const GoldenMove a = play_move(d.table.game, d.seat, known, d.table.memory, *d.played, 0, 24, 5, {});
    const GoldenMove b = play_move(back, d.seat, known, d.table.memory, *d.played, 0, 24, 5, {});
    EXPECT_EQ(a.confirm.wins, b.confirm.wins);
    const TableMemory m = memory_from_json(memory_json(d.table.memory));
    EXPECT_EQ(memory_json(m), memory_json(d.table.memory));
  }
}

}  // namespace
}  // namespace cae
