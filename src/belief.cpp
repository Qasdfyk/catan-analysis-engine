#include "catan/belief.hpp"

#include <algorithm>
#include <utility>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

int total(const Cards& c) noexcept {
  int n = 0;
  for (const int x : c) n += x;
  return n;
}

// Deals `count` cards from `pool` (cards of each kind left) at random, each
// remaining card equally likely, into `into`.
template <std::size_t N>
void deal(std::array<int, N>& pool, int count, std::array<int, N>& into, Rng& rng) {
  int left = 0;
  for (const int x : pool) left += x;
  for (int k = 0; k < count && left > 0; ++k) {
    int pick = rng.below(left);
    std::size_t kind = 0;
    while (pick >= pool[kind]) pick -= pool[kind++];
    --pool[kind];
    ++into[kind];
    --left;
  }
}

template <std::size_t N>
std::array<std::uint8_t, N> as_bytes(const std::array<int, N>& a) noexcept {
  std::array<std::uint8_t, N> out{};
  for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<std::uint8_t>(a[i]);
  return out;
}

int held_count(const Player& p) noexcept {
  int n = 0;
  for (std::size_t c = 0; c < p.dev_cards.size(); ++c) n += p.dev_cards[c] + p.new_dev_cards[c];
  return n;
}

}  // namespace

void HandBelief::observe(const Game& before, const Game& after) noexcept {
  const int n = after.player_count;
  // A steal is the one move that takes a card nobody else sees. It is the
  // move that leaves the kSteal phase with a card moved.
  int thief = -1;
  int victim = -1;
  if (before.phase == Phase::kSteal && after.phase != Phase::kSteal) {
    for (int s = 0; s < n; ++s) {
      if (after.players[at(s)].card_count() < before.players[at(s)].card_count()) victim = s;
    }
    if (victim >= 0) thief = before.current;
  }
  for (int o = 0; o < n; ++o) {
    for (int p = 0; p < n; ++p) {
      Cards& k = known[at(o)][at(p)];
      const Cards& now = after.players[at(p)].resources;
      if (p == o) {
        k = now;
        continue;
      }
      const bool unseen = thief >= 0 && o != thief && o != victim;
      if (unseen && p == victim) {
        // One of the victim's cards went, nobody here saw which.
        for (int& x : k) x = std::max(0, x - 1);
        continue;
      }
      if (unseen && p == thief) continue;  // a card of unknown kind came in
      const Cards& was = before.players[at(p)].resources;
      for (std::size_t r = 0; r < k.size(); ++r) k[r] = std::max(0, k[r] + now[r] - was[r]);
    }
  }
}

std::array<int, kDevCardKinds> played_dev_cards(const Game& game) noexcept {
  std::array<int, kDevCardKinds> played{};
  for (int i = 0; i < game.dev_deck_next; ++i) ++played[at(static_cast<int>(game.dev_deck[at(i)]))];
  for (int s = 0; s < game.player_count; ++s) {
    const Player& p = game.players[at(s)];
    for (std::size_t c = 0; c < played.size(); ++c) played[c] -= p.dev_cards[c] + p.new_dev_cards[c];
  }
  return played;
}

Game determinize(const Game& truth, int observer, const std::array<Cards, kMaxPlayers>& known, Rng& rng) {
  const int n = truth.player_count;

  // Resources the opponents hold together, minus the cards each surely has.
  Cards pool{};
  for (std::size_t r = 0; r < pool.size(); ++r) {
    pool[r] = kBankSize - truth.bank_count(static_cast<Resource>(r)) - truth.players[at(observer)].resources[r];
  }
  for (int p = 0; p < n; ++p) {
    if (p == observer) continue;
    for (std::size_t r = 0; r < pool.size(); ++r) pool[r] -= known[at(p)][r];
  }

  // Development cards the observer has not seen: the whole deck minus the
  // cards played and the observer's own.
  std::array<int, kDevCardKinds> unseen = kDevDeckComposition;
  const auto played = played_dev_cards(truth);
  const Player& me = truth.players[at(observer)];
  for (std::size_t c = 0; c < unseen.size(); ++c) unseen[c] -= played[c] + me.dev_cards[c] + me.new_dev_cards[c];

  Game g = truth;
  constexpr int kTries = 16;
  for (int attempt = 0; attempt < kTries; ++attempt) {
    g = truth;
    Cards res_left = pool;
    std::array<int, kDevCardKinds> dev_left = unseen;
    bool plausible = true;
    for (int p = 0; p < n; ++p) {
      if (p == observer) continue;
      Player& pl = g.players[at(p)];
      const Player& real = truth.players[at(p)];
      Cards hand = known[at(p)];
      deal(res_left, real.card_count() - total(hand), hand, rng);
      pl.resources = hand;
      // Cards bought this turn stay apart from older ones: they cannot be
      // played yet. How many of each sort a seat holds is public.
      int fresh_count = 0;
      for (const auto x : real.new_dev_cards) fresh_count += x;
      std::array<int, kDevCardKinds> fresh{};
      std::array<int, kDevCardKinds> old{};
      deal(dev_left, fresh_count, fresh, rng);
      deal(dev_left, held_count(real) - fresh_count, old, rng);
      pl.new_dev_cards = as_bytes(fresh);
      pl.dev_cards = as_bytes(old);
      if (g.visible_points(p) + g.hidden_points(p) >= g.settings.victory_points) plausible = false;
    }
    // The drawn part of the deck: the cards played and the cards now held,
    // so that it adds up as the game does (its order does not matter).
    int i = 0;
    std::array<int, kDevCardKinds> drawn = played;
    for (int p = 0; p < n; ++p) {
      const Player& pl = g.players[at(p)];
      for (std::size_t c = 0; c < drawn.size(); ++c) drawn[c] += pl.dev_cards[c] + pl.new_dev_cards[c];
    }
    for (std::size_t c = 0; c < drawn.size(); ++c) {
      for (int k = 0; k < drawn[c] && i < g.dev_deck_next; ++k) g.dev_deck[at(i++)] = static_cast<DevCard>(c);
    }
    // The rest of the unseen cards is the deck, in a random order.
    i = g.dev_deck_next;
    for (std::size_t c = 0; c < dev_left.size(); ++c) {
      for (int k = 0; k < dev_left[c] && i < g.dev_deck_size; ++k) g.dev_deck[at(i++)] = static_cast<DevCard>(c);
    }
    for (int j = g.dev_deck_size - 1; j > g.dev_deck_next; --j) {
      const int m = g.dev_deck_next + rng.below(j - g.dev_deck_next + 1);
      std::swap(g.dev_deck[at(j)], g.dev_deck[at(m)]);
    }
    if (plausible) break;
  }
  return g;
}

}  // namespace cae
