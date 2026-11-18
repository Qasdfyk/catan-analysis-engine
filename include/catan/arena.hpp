#pragma once

// The arena: fair matches between players, and a sequential test that stops
// as soon as the result is clear.
//
// A match puts one candidate at a table with opponents and counts how often
// it wins. In a four-player game an equal candidate wins 25%. The trouble is
// noise: the board, the seat and the dice decide much of a game, so telling
// 27% from 25% takes thousands of games. Two things cut that down.
//
// Duplicates (as in duplicate bridge). One deal is a board, a development
// card deck and a sequence of dice. The candidate plays the same deal four
// times, once from each seat, against the same opponents. Whatever luck the
// deal holds, every seat gets it once. The unit of the match is then the
// deal, scored as the share of its four games the candidate won.
//   kPlain      every game its own deal, the candidate on a random seat
//   kRotate     same board and deck in four games, the candidate in every
//               seat, new dice each game
//   kDuplicate  as kRotate, and the k-th roll of the game is the same number
//               in all four games
// The dice of a duplicate game follow the deal, not the game: roll k of every
// game of the deal is the same pair. Everything else (stolen cards, a bot's
// random fallback) comes from the game's own stream.
//
// SPRT (sequential probability ratio test, Wald 1945; fishtest uses it to
// test Stockfish patches). Two hypotheses about the candidate's win rate,
// H0: p = p0 and H1: p = p1. After each batch of deals the log-likelihood
// ratio of the results under H1 and H0 is compared with two bounds; crossing
// the upper one accepts H1, the lower one H0, and in between the match goes
// on. With error rates alpha and beta the bounds are ln(beta / (1 - alpha))
// and ln((1 - beta) / alpha). The scores of a deal are not 0 or 1, so the
// ratio uses a normal model of the deal scores with their measured variance
// (the generalized SPRT of fishtest's pentanomial model):
//   LLR = n (p1 - p0) (2 mean - p0 - p1) / (2 var)
// for n deals with mean score `mean` and variance `var`.

#include "catan/game.hpp"
#include "catan/rng.hpp"
#include "catan/zoo.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cae {

enum class DealMode : std::uint8_t { kPlain, kRotate, kDuplicate };

// What a deal fixes: the board, the deck, who starts, the dice stream.
struct Deal {
  Board board;
  std::array<DevCard, kDevDeckSize> deck{};
  int first_seat = 0;
  std::uint64_t dice_seed = 0;
  std::uint64_t seed = 0;  // for everything else; each game of the deal mixes in its own number
};

// Deal `index` of a match with `seed`. The same arguments give the same deal.
[[nodiscard]] Deal make_deal(std::uint64_t seed, int index, int players = 4);

// The dice of roll `k` (0 = the first roll of the game) in a deal.
[[nodiscard]] std::array<int, 2> deal_dice(std::uint64_t dice_seed, int k) noexcept;

// A piece on the board, in the order the game built them. Bots that keep
// their own board (Catanatron, through the bridge) rebuild it from this.
struct Built {
  enum class Kind : std::uint8_t { kSettlement, kCity, kRoad } kind = Kind::kSettlement;
  int seat = 0;
  int at = 0;  // node or edge
};

// What a player sees when asked for a move.
struct ArenaView {
  const Game& game;
  int seat;
  const TableMemory& memory;
  std::span<const Built> built;
};

// A player: the next move for the seat, or nothing to let the table play the
// safe move (end the turn, decline an offer, a random legal move otherwise).
using ArenaPlayer = std::function<std::optional<Action>(const ArenaView&)>;

// A zoo bot as an arena player. With `trades` false it never offers a trade
// to players and turns down every offer (Catanatron has no trading between
// players, so matches against it are played without).
[[nodiscard]] ArenaPlayer zoo_player(const ZooBot& bot, bool trades = true);

// How a seat played a game: what MAP-Elites (map_elites.hpp) sorts bots by.
struct SeatPlay {
  int settlements = 0;  // standing at the end (a city is no longer a settlement)
  int cities = 0;
  int trades = 0;     // trades with players completed, as proposer or partner
  int dev_cards = 0;  // development cards bought
};

struct ArenaGame {
  int winner = -1;  // -1 when the action limit ran out
  int actions = 0;
  int turns = 0;
  std::array<int, kMaxPlayers> fallbacks{};  // moves of each seat refused or missing
  std::array<int, kMaxPlayers> points{};     // victory points at the end, hidden ones included
  std::array<SeatPlay, kMaxPlayers> play{};
};

// One game of a deal. `game_number` picks the game's own random stream; with
// `fixed_dice` the rolls come from the deal (kDuplicate).
[[nodiscard]] ArenaGame play_arena_game(const Deal& deal, std::span<const ArenaPlayer> seats, int game_number,
                                        bool fixed_dice, int max_actions = 40'000);

// --- Statistics over deals ------------------------------------------------

// Mean and variance of deal scores, added up deal by deal.
struct ScoreStats {
  long n = 0;
  double sum = 0;
  double sum_sq = 0;

  void add(double score) noexcept {
    ++n;
    sum += score;
    sum_sq += score * score;
  }
  [[nodiscard]] double mean() const noexcept { return n > 0 ? sum / static_cast<double>(n) : 0.0; }
  // Sample variance (n - 1 in the denominator).
  [[nodiscard]] double variance() const noexcept;
  // Standard error of the mean.
  [[nodiscard]] double std_error() const noexcept;
};

struct Sprt {
  double p0 = 0.25;
  double p1 = 0.30;
  double alpha = 0.05;
  double beta = 0.05;

  [[nodiscard]] double lower() const noexcept;  // ln(beta / (1 - alpha))
  [[nodiscard]] double upper() const noexcept;  // ln((1 - beta) / alpha)
  // The log-likelihood ratio of H1 against H0 for these deal scores; 0 until
  // there are two deals with some spread.
  [[nodiscard]] double llr(const ScoreStats& s) const noexcept;
};

enum class SprtResult : std::uint8_t { kContinue, kH0, kH1 };
[[nodiscard]] SprtResult sprt_decide(const Sprt& test, const ScoreStats& s) noexcept;

}  // namespace cae
