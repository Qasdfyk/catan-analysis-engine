#pragma once

// MAP-Elites over the zoo (step 10c of the plan): the best bot of every style.
//
// The zoo's archetypes were set by hand. Tuning them for wins alone would
// push them all toward the one strongest style, and the simulations need
// opponents that play in different ways. MAP-Elites (Mouret and Clune, 2015)
// keeps both: the space of styles is cut into cells, and every cell keeps
// its own elite, the strongest bot seen that plays in that style. A new bot
// is a mutated copy of a random elite; it is played, its style says which
// cell it belongs to, and it takes the cell if it is stronger than the elite
// there (or the cell was empty).
//
// The style is measured, not declared: a bot belongs where its games put it.
// Three numbers, averaged over its games:
//   city share  the share of its building points that come from cities
//               (2 per city against 1 per settlement) at the end of a game
//   trades      trades with players completed per game
//   dev cards   development cards bought per game
//
// Strength is the win rate against three random zoo bots (25% is even), on
// the deals of the arena (arena.hpp). It is noisy: 400 games give about +-4
// points (95%). A cell's elite is the bot that looked best, and "looked
// best" includes luck, so the stored score of an elite is too high (the
// winner's curse). Two things work against it:
//  - the recheck: a bot that beats an elite on the shared search deals plays
//    a set of fresh deals of its own, and the cell goes to it only if it wins
//    there too. That second score was also used to choose, so it is cursed
//    as well, only less;
//  - re-evaluation: every generation each elite plays more fresh deals and
//    adds them to its score. Games played after a choice carry no luck of
//    that choice, so a lucky elite sinks back to what it is worth and the
//    next good candidate takes its cell (the archive re-evaluation of noisy
//    quality-diversity, Flageat and Cully 2023).
// At the end every elite plays a large final set of deals that no choice was
// made on: its honest score.

#include "catan/arena.hpp"
#include "catan/rng.hpp"
#include "catan/zoo.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cae {

// --- The grid of styles ----------------------------------------------------

// Bin edges on each axis: a value below edges[0] is bin 0, at or above
// edges.back() the last bin. Chosen from the spread of the hand-made zoo
// (docs/map-elites.md), so every bin is reachable.
// The hand-made zoo spans city shares 0.39-0.89, 0-11 trades and 1-6 cards.
inline constexpr std::array<double, 4> kCityShareEdges{0.45, 0.6, 0.72, 0.84};
inline constexpr std::array<double, 3> kTradeEdges{0.5, 3.0, 7.0};
inline constexpr std::array<double, 3> kDevCardEdges{1.5, 3.0, 5.0};
inline constexpr int kCityBins = static_cast<int>(kCityShareEdges.size()) + 1;
inline constexpr int kTradeBins = static_cast<int>(kTradeEdges.size()) + 1;
inline constexpr int kDevCardBins = static_cast<int>(kDevCardEdges.size()) + 1;
inline constexpr int kStyleCells = kCityBins * kTradeBins * kDevCardBins;

struct Behavior {
  double city_share = 0;
  double trades = 0;
  double dev_cards = 0;
};

struct CellBins {
  int city = 0;
  int trades = 0;
  int dev_cards = 0;
};

[[nodiscard]] CellBins bins_of(const Behavior& b) noexcept;
// Cell index: city + kCityBins * (trades + kTradeBins * dev_cards).
[[nodiscard]] int cell_of(const CellBins& b) noexcept;
[[nodiscard]] CellBins bins_of_cell(int cell) noexcept;

// --- Evaluation ------------------------------------------------------------

// What a bot's games added up to.
struct Evaluation {
  long games = 0;
  long wins = 0;
  long unfinished = 0;
  double city_share = 0;  // sums over the games
  double trades = 0;
  double dev_cards = 0;

  Evaluation& operator+=(const Evaluation& o) noexcept;
  [[nodiscard]] double win_rate() const noexcept {
    return games > 0 ? static_cast<double>(wins) / static_cast<double>(games) : 0.0;
  }
  // Half-width of the 95% interval of the win rate.
  [[nodiscard]] double margin() const noexcept;
  [[nodiscard]] Behavior behavior() const noexcept;
};

// Three random zoo bots for deal `deal_seed`: slot 0, 1, 2 drawn like the
// `zoo` opponents of catan_arena, so the same deal gives the same table.
// With a non-empty `pool` each is drawn from the pool instead.
[[nodiscard]] std::array<ZooBot, 3> deal_opponents(std::uint64_t deal_seed, std::span<const ZooBot> pool = {});

// Deal `index` of `seed`, plain mode: one game, `bot` on a random seat
// against deal_opponents().
[[nodiscard]] Evaluation evaluate_deal(const ZooBot& bot, std::uint64_t seed, int index,
                                       std::span<const ZooBot> pool = {});

// --- The search ------------------------------------------------------------

// A copy of `parent` with some genes changed. Weights are multiplied by
// exp(0.25 z) (z standard normal) with probability 1/2 each, the Robber's
// leader focus moves by 0.5 z, whole-number genes by one step, and the
// yes/no genes of trading flip now and then. Every gene stays in a sane
// range. The child gets seed 0 (its weights are its own, nothing to shake).
[[nodiscard]] ZooBot mutate(const ZooBot& parent, Rng& rng);

struct Elite {
  ZooBot bot;
  Evaluation search;  // on the shared search deals: what got it noticed
  // On its own fresh deals: the recheck and every re-evaluation since.
  // Empty when both are off.
  Evaluation fresh;
  Evaluation final;  // on the final deals, after the search
  int born = 0;      // the candidate number that made it; picks its fresh deals
  int parent = -1;   // the cell of the elite it was mutated from; -1 for a seed bot
  // The score it is compared by: fresh if there are fresh games, else search.
  [[nodiscard]] const Evaluation& score() const noexcept { return fresh.games > 0 ? fresh : search; }
};

// The seed of the fresh deals of candidate `born` in a search with `seed`:
// every candidate has its own stream, so its recheck and re-evaluations
// never share a deal with anyone's.
[[nodiscard]] std::uint64_t fresh_seed(std::uint64_t seed, int born) noexcept;

struct Archive {
  std::array<std::optional<Elite>, kStyleCells> cells{};

  [[nodiscard]] int filled() const noexcept;
  // The cells that hold an elite, in index order.
  [[nodiscard]] std::vector<int> filled_cells() const;
  // Mean of the elites' scores: the "QD score" divided by the cells filled.
  [[nodiscard]] double mean_score() const noexcept;
};

}  // namespace cae
