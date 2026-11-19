#pragma once

// What a seat knows about the other hands, and hands drawn to match it.
//
// A player at the table sees the bank, everyone's card count, the cards
// that change hands in public (production, building, trades, discards,
// monopolies) and the development cards played. Two things stay hidden:
//  - a card stolen between two other players (the thief and the victim see
//    it, nobody else does);
//  - which development cards a player holds, and the order of the deck.
//
// Resources. The bank is public and so is the observer's own hand, so the
// cards of each resource the opponents hold together are known exactly:
// 19 - bank - own. What is not known is how they are split among the
// opponents. HandBelief keeps, for every observer and every other seat, a
// lower bound per resource: cards that seat holds for sure. Every public
// change moves the bound with it. A steal the observer did not take part in
// takes one card nobody saw from the victim, so every bound of the victim
// drops by one (it was one of them, but which is unknown) and the thief
// gains a card of unknown kind. The bounds never exceed the true hand
// (tests/belief_test.cpp holds them to that over whole games).
//
// Determinization (Cowling, Powley and Whitehouse 2012, "Information Set
// Monte Carlo Tree Search"). A search cannot play from a hidden state, so it
// draws one: a full game state that agrees with everything the observer
// knows. determinize() keeps the board, the observer's hand, the bank and
// every count, puts each opponent's sure cards in their hand, deals the rest
// of the opponents' cards at random to fill each hand to its count, and
// deals the opponents' development cards and the deck from the cards the
// observer has not seen. Nothing hidden from the observer reaches the
// result: two games that differ only in hidden cards give the same states
// for the same random stream (also a test).

#include "catan/game.hpp"
#include "catan/rng.hpp"

#include <array>

namespace cae {

// known[observer][seat]: cards of each resource `seat` holds for sure, as far
// as `observer` can tell. known[s][s] is s's own hand.
struct HandBelief {
  std::array<std::array<Cards, kMaxPlayers>, kMaxPlayers> known{};

  // Brings every bound from `before` to `after`, one action apart. Whether
  // the action was a steal is read from the states (the phase was kSteal),
  // so a fallback move of the arena is tracked like any other.
  void observe(const Game& before, const Game& after) noexcept;
};

// The development cards in a game of catan-project: 14 knights, 5 victory
// points, 2 each of road building, year of plenty and monopoly.
inline constexpr std::array<int, kDevCardKinds> kDevDeckComposition = {14, 5, 2, 2, 2};

// Development cards of each kind played so far by anyone. Public at the
// table; computed here from the drawn cards and the hands, which adds up to
// the same number.
[[nodiscard]] std::array<int, kDevCardKinds> played_dev_cards(const Game& game) noexcept;

// A state drawn from what `observer` knows about `truth`, given its belief
// `known` (HandBelief::known[observer]). Hands that would put an opponent at
// the winning score (which the game would already have ended on) are
// redrawn, a few times at most.
[[nodiscard]] Game determinize(const Game& truth, int observer, const std::array<Cards, kMaxPlayers>& known,
                               Rng& rng);

}  // namespace cae
