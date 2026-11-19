// What a seat knows of the other hands: the bounds are never wrong, and a
// drawn state agrees with everything public and depends on nothing hidden.

#include "catan/belief.hpp"
#include "catan/random_player.hpp"
#include "catan/zoo.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace cae {
namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

ZooTable new_table(std::uint64_t seed) {
  Rng rng(seed);
  ZooTable t;
  const Board board = random_board(rng);
  const auto deck = random_dev_deck(rng);
  t.game = Game::create(board, Settings{}, 4, rng.below(4), deck);
  for (auto& b : t.bots) b = random_zoo_bot(rng);
  return t;
}

// Every state of a few zoo games, with the belief after it.
struct Tracked {
  std::vector<Game> states;
  std::vector<HandBelief> beliefs;
  int unseen_steals = 0;
};

Tracked track(std::uint64_t seed) {
  Tracked out;
  ZooTable t = new_table(seed);
  Rng rng(seed + 1000);
  HandBelief belief;
  for (int step = 0; step < 40'000 && !t.game.game_over; ++step) {
    const Game before = t.game;
    (void)play_zoo_step(t, rng);
    if (before.phase == Phase::kSteal && t.game.phase != Phase::kSteal) ++out.unseen_steals;
    belief.observe(before, t.game);
    out.states.push_back(t.game);
    out.beliefs.push_back(belief);
  }
  return out;
}

TEST(HandBelief, BoundsNeverExceedTheTrueHand) {
  int steals = 0;
  long sure = 0;
  long held = 0;
  for (std::uint64_t seed = 1; seed <= 6; ++seed) {
    const Tracked tr = track(seed);
    steals += tr.unseen_steals;
    for (std::size_t i = 0; i < tr.states.size(); ++i) {
      const Game& g = tr.states[i];
      for (int o = 0; o < 4; ++o) {
        for (int p = 0; p < 4; ++p) {
          const Cards& k = tr.beliefs[i].known[at(o)][at(p)];
          const Cards& real = g.players[at(p)].resources;
          for (std::size_t r = 0; r < k.size(); ++r) {
            ASSERT_LE(k[r], real[r]) << "seed " << seed << " state " << i << " observer " << o << " seat " << p;
            ASSERT_GE(k[r], 0);
          }
          if (o == p) {
            EXPECT_EQ(k, real);
          } else {
            for (std::size_t r = 0; r < k.size(); ++r) {
              sure += k[r];
              held += real[r];
            }
          }
        }
      }
    }
  }
  EXPECT_GT(steals, 10);  // the steal rule was exercised
  // Most cards are known: only steals between others hide any.
  EXPECT_GT(static_cast<double>(sure) / static_cast<double>(held), 0.5);
}

// Checks that `d` agrees with everything `observer` knows of `truth`.
void expect_consistent(const Game& truth, const Game& d, int observer, const std::array<Cards, kMaxPlayers>& known) {
  for (int r = 0; r < kResourceCount; ++r) {
    EXPECT_EQ(d.bank_count(static_cast<Resource>(r)), truth.bank_count(static_cast<Resource>(r)));
  }
  EXPECT_EQ(played_dev_cards(d), played_dev_cards(truth));
  for (int p = 0; p < truth.player_count; ++p) {
    const Player& a = truth.players[at(p)];
    const Player& b = d.players[at(p)];
    EXPECT_EQ(a.card_count(), b.card_count());
    EXPECT_EQ(a.settlements, b.settlements);
    EXPECT_EQ(a.knights, b.knights);
    int held_a = 0, held_b = 0, new_a = 0, new_b = 0;
    for (std::size_t c = 0; c < a.dev_cards.size(); ++c) {
      held_a += a.dev_cards[c] + a.new_dev_cards[c];
      held_b += b.dev_cards[c] + b.new_dev_cards[c];
      new_a += a.new_dev_cards[c];
      new_b += b.new_dev_cards[c];
    }
    EXPECT_EQ(held_a, held_b);
    EXPECT_EQ(new_a, new_b);
    if (p == observer) {
      EXPECT_EQ(a.resources, b.resources);
      EXPECT_EQ(a.dev_cards, b.dev_cards);
      EXPECT_EQ(a.new_dev_cards, b.new_dev_cards);
    } else {
      for (std::size_t r = 0; r < kResourceCount; ++r) EXPECT_GE(b.resources[r], known[at(p)][r]);
    }
  }
  // The deck: the drawn part is what it was, the whole holds the right cards.
  std::array<int, kDevCardKinds> all{};
  for (int i = 0; i < d.dev_deck_size; ++i) ++all[at(static_cast<int>(d.dev_deck[at(i)]))];
  EXPECT_EQ(all, kDevDeckComposition);
}

TEST(Determinize, AgreesWithWhatTheSeatKnows) {
  const Tracked tr = track(11);
  Rng rng(3);
  for (std::size_t i = 0; i < tr.states.size(); i += 97) {
    const Game& g = tr.states[i];
    for (int o = 0; o < 4; ++o) {
      SCOPED_TRACE(testing::Message() << "state " << i << " observer " << o);
      const Game d = determinize(g, o, tr.beliefs[i].known[at(o)], rng);
      expect_consistent(g, d, o, tr.beliefs[i].known[at(o)]);
    }
  }
}

// Two games that differ only in what the observer cannot see (here: one
// drawn from the other) give the same drawn state for the same stream.
TEST(Determinize, DependsOnNothingHidden) {
  const Tracked tr = track(12);
  int compared = 0;
  int hidden_differs = 0;
  for (std::size_t i = 0; i < tr.states.size(); i += 53) {
    const Game& g = tr.states[i];
    for (int o = 0; o < 4; ++o) {
      const auto& known = tr.beliefs[i].known[at(o)];
      Rng other(i * 10 + static_cast<std::size_t>(o));
      const Game twin = determinize(g, o, known, other);
      bool differs = false;
      for (int p = 0; p < 4; ++p) {
        differs |= twin.players[at(p)].resources != g.players[at(p)].resources;
        differs |= twin.players[at(p)].dev_cards != g.players[at(p)].dev_cards;
      }
      hidden_differs += differs ? 1 : 0;
      Rng a(77);
      Rng b(77);
      const Game x = determinize(g, o, known, a);
      const Game y = determinize(twin, o, known, b);
      for (int p = 0; p < 4; ++p) {
        ASSERT_EQ(x.players[at(p)].resources, y.players[at(p)].resources);
        ASSERT_EQ(x.players[at(p)].dev_cards, y.players[at(p)].dev_cards);
        ASSERT_EQ(x.players[at(p)].new_dev_cards, y.players[at(p)].new_dev_cards);
      }
      ASSERT_EQ(x.dev_deck, y.dev_deck);
      ++compared;
    }
  }
  EXPECT_GT(compared, 20);
  EXPECT_GT(hidden_differs, 5);  // the twins really were different games
}

}  // namespace
}  // namespace cae
