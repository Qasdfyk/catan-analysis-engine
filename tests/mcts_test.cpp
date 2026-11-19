// The searching bot: reproducible, legal, sees nothing hidden, takes a sure
// win, and plays whole arena games without a refused move.

#include "catan/mcts.hpp"
#include "catan/random_player.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace cae {
namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// A zoo game played until `stop` says so; the belief is tracked on the way.
struct Position {
  Game game;
  HandBelief belief;
  TableMemory memory;
  bool found = false;
};

template <typename Stop>
Position find_position(std::uint64_t seed, Stop stop) {
  Rng rng(seed);
  ZooTable t;
  const Board board = random_board(rng);
  const auto deck = random_dev_deck(rng);
  t.game = Game::create(board, Settings{}, 4, rng.below(4), deck);
  for (auto& b : t.bots) b = hard_bot();
  Position out;
  for (int step = 0; step < 40'000 && !t.game.game_over; ++step) {
    if (stop(t.game)) {
      out.game = t.game;
      out.memory = t.memory;
      out.memory.sync(t.game);
      out.found = true;
      return out;
    }
    const Game before = t.game;
    (void)play_zoo_step(t, rng);
    out.belief.observe(before, t.game);
  }
  return out;
}

// A main-phase decision with several moves, some way into the game.
Position midgame(std::uint64_t seed) {
  return find_position(seed, [](const Game& g) {
    return g.turn_number > 30 && g.phase == Phase::kMain && !g.offer.open &&
           search_candidates(g, g.current, std::nullopt).size() >= 4;
  });
}

MctsSettings small(int iterations) {
  MctsSettings s;
  s.iterations = iterations;
  return s;
}

TEST(Mcts, SameSeedSameSearch) {
  const Position p = midgame(1);
  ASSERT_TRUE(p.found);
  const int seat = p.game.current;
  const auto& known = p.belief.known[at(seat)];
  const MctsSearch a = mcts_search(p.game, seat, known, p.memory, hard_bot(), {}, small(60), 5);
  const MctsSearch b = mcts_search(p.game, seat, known, p.memory, hard_bot(), {}, small(60), 5);
  ASSERT_EQ(a.root.size(), b.root.size());
  for (std::size_t i = 0; i < a.root.size(); ++i) {
    EXPECT_EQ(a.root[i].action, b.root[i].action);
    EXPECT_EQ(a.root[i].visits, b.root[i].visits);
    EXPECT_EQ(a.root[i].wins, b.root[i].wins);
  }
  EXPECT_EQ(a.best, b.best);
}

TEST(Mcts, EveryIterationVisitsOneLegalRootMove) {
  for (std::uint64_t seed = 2; seed < 5; ++seed) {
    const Position p = midgame(seed);
    ASSERT_TRUE(p.found);
    const int seat = p.game.current;
    const MctsSearch s =
        mcts_search(p.game, seat, p.belief.known[at(seat)], p.memory, hard_bot(), {}, small(80), seed);
    int visits = 0;
    for (const MctsMove& m : s.root) visits += m.visits;
    EXPECT_EQ(visits, 80);
    Game copy = p.game;
    Rng rng(1);
    EXPECT_EQ(apply_action(copy, seat, s.best, rng), Error::kNone) << action_name(s.best.kind);
  }
}

// The search reads the seat's belief, never the hidden cards: a game that
// differs only in what the seat cannot see gives the same search.
TEST(Mcts, SeesNothingHidden) {
  const Position p = midgame(6);
  ASSERT_TRUE(p.found);
  const int seat = p.game.current;
  const auto& known = p.belief.known[at(seat)];
  Rng other(99);
  const Game twin = determinize(p.game, seat, known, other);
  const MctsSearch a = mcts_search(p.game, seat, known, p.memory, hard_bot(), {}, small(40), 8);
  const MctsSearch b = mcts_search(twin, seat, known, p.memory, hard_bot(), {}, small(40), 8);
  ASSERT_EQ(a.root.size(), b.root.size());
  for (std::size_t i = 0; i < a.root.size(); ++i) {
    EXPECT_EQ(a.root[i].visits, b.root[i].visits);
    EXPECT_EQ(a.root[i].wins, b.root[i].wins);
  }
}

// One building away from ten points: the search takes the win.
TEST(Mcts, TakesASureWin) {
  int checked = 0;
  for (std::uint64_t seed = 1; seed < 40 && checked < 3; ++seed) {
    const Position p = find_position(seed, [](const Game& g) {
      const int s = g.current;
      return g.phase == Phase::kMain && !g.offer.open &&
             g.visible_points(s) + g.hidden_points(s) == g.settings.victory_points - 1 &&
             (g.legal_cities(s) != 0 || g.legal_settlements(s) != 0);
    });
    if (!p.found) continue;
    ++checked;
    const int seat = p.game.current;
    const MctsSearch s =
        mcts_search(p.game, seat, p.belief.known[at(seat)], p.memory, hard_bot(), {}, small(200), seed);
    EXPECT_TRUE(s.best.kind == ActionKind::kUpgradeCity || s.best.kind == ActionKind::kBuildSettlement)
        << action_name(s.best.kind);
    const auto it = std::find_if(s.root.begin(), s.root.end(), [&](const MctsMove& m) { return m.action == s.best; });
    ASSERT_NE(it, s.root.end());
    EXPECT_EQ(it->wins, it->visits);
  }
  EXPECT_GT(checked, 0);
}

TEST(Mcts, MergedSearchesAddUp) {
  const Position p = midgame(7);
  ASSERT_TRUE(p.found);
  const int seat = p.game.current;
  const auto& known = p.belief.known[at(seat)];
  MctsSearch a = mcts_search(p.game, seat, known, p.memory, hard_bot(), {}, small(30), 1);
  const MctsSearch b = mcts_search(p.game, seat, known, p.memory, hard_bot(), {}, small(30), 2);
  merge_searches(a, b);
  int visits = 0;
  for (const MctsMove& m : a.root) visits += m.visits;
  EXPECT_EQ(visits, 60);
  EXPECT_EQ(a.iterations, 60);
}

TEST(Mcts, PlaysAWholeArenaGame) {
  MctsSettings s = small(16);
  std::vector<ArenaPlayer> table(4, zoo_player(hard_bot()));
  table[0] = mcts_player(hard_bot(), {}, s, 3);
  const ArenaGame g = play_arena_game(make_deal(4, 1), table, 0, false);
  EXPECT_GE(g.winner, 0);
  EXPECT_EQ(g.fallbacks, (std::array<int, kMaxPlayers>{}));
}

}  // namespace
}  // namespace cae
