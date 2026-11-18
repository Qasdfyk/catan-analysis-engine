#include "catan/arena.hpp"

#include "catan/random_player.hpp"

#include <cmath>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// Two numbers into one seed. Rng scrambles its seed with splitmix64, so
// neighboring results still give unrelated streams.
constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  return a * 0x9e3779b97f4a7c15ULL + b + 0x632be59bd9b4e019ULL;
}

Action make(ActionKind kind, int target = -1) {
  Action a;
  a.kind = kind;
  a.target = target;
  return a;
}

// Pieces a step added: one at most, since every action builds at most one.
// Comparing the masks before and after also catches what a random fallback
// built, which no Action describes.
void note_new_pieces(const Game& before, const Game& after, std::vector<Built>& built, TableMemory& memory) {
  for (int s = 0; s < after.player_count; ++s) {
    const Player& was = before.players[at(s)];
    const Player& now = after.players[at(s)];
    for_each_node(now.settlements & ~was.settlements, [&](int n) {
      built.push_back({Built::Kind::kSettlement, s, n});
      memory.record(s, make(ActionKind::kBuildSettlement, n));
    });
    for_each_node(now.cities & ~was.cities, [&](int n) { built.push_back({Built::Kind::kCity, s, n}); });
    for_each_edge(now.roads & ~was.roads, [&](int e) { built.push_back({Built::Kind::kRoad, s, e}); });
  }
}

}  // namespace

Deal make_deal(std::uint64_t seed, int index, int players) {
  Rng rng(mix(seed, static_cast<std::uint64_t>(index)));
  Deal d;
  d.board = random_board(rng);
  d.deck = random_dev_deck(rng);
  d.first_seat = rng.below(players);
  d.dice_seed = rng.next();
  d.seed = rng.next();
  return d;
}

std::array<int, 2> deal_dice(std::uint64_t dice_seed, int k) noexcept {
  Rng rng(mix(dice_seed, static_cast<std::uint64_t>(k)));
  const int first = rng.die();
  return {first, rng.die()};
}

ArenaPlayer zoo_player(const ZooBot& bot, bool trades) {
  ZooBot b = bot;
  if (!trades) b.trading.offers_per_turn = 0;
  return [b, trades](const ArenaView& v) -> std::optional<Action> {
    auto a = zoo_decide(v.game, v.seat, b, v.memory);
    if (!trades && a && a->kind == ActionKind::kRespondTrade) a->accept = false;
    return a;
  };
}

ArenaGame play_arena_game(const Deal& deal, std::span<const ArenaPlayer> seats, int game_number, bool fixed_dice,
                          int max_actions) {
  const int players = static_cast<int>(seats.size());
  Game game = Game::create(deal.board, Settings{}, players, deal.first_seat, deal.deck);
  Rng rng(mix(deal.seed, static_cast<std::uint64_t>(game_number)));
  TableMemory memory;
  std::vector<Built> built;
  built.reserve(2 * kMaxPlayers * (kMaxSettlements + kMaxRoads));
  int rolls = 0;
  ArenaGame out;

  // A roll: from the deal with fixed dice, else from the game's stream.
  const auto roll = [&](int seat) {
    if (!fixed_dice) return apply_action(game, seat, make(ActionKind::kRollDice), rng);
    const auto d = deal_dice(deal.dice_seed, rolls);
    return game.roll_dice(seat, d[0], d[1]);
  };

  while (!game.game_over && out.actions < max_actions) {
    const int seat = next_actor(game);
    if (seat < 0) break;
    memory.sync(game);
    const Game before = game;
    const std::optional<Action> action = seats[at(seat)](ArenaView{game, seat, memory, built});
    Error e = Error::kBadArgument;
    if (action) {
      e = action->kind == ActionKind::kRollDice ? roll(seat) : apply_action(game, seat, *action, rng);
    }
    if (e == Error::kNone) {
      if (action->kind == ActionKind::kRollDice) ++rolls;
      if (action->kind != ActionKind::kBuildSettlement) memory.record(seat, *action);
    } else {
      // The safe move, as after a refused move on the platform.
      ++out.fallbacks[at(seat)];
      const TradeOffer& offer = game.offer;
      if (game.phase == Phase::kRollDice && seat == game.current) {
        if (roll(seat) == Error::kNone) ++rolls;
      } else if (offer.open && offer.proposer != seat) {
        Action no = make(ActionKind::kRespondTrade);
        no.accept = false;
        (void)apply_action(game, seat, no, rng);
      } else if (offer.open) {
        (void)apply_action(game, seat, make(ActionKind::kCancelTrade), rng);
      } else if (game.phase != Phase::kMain || seat != game.current ||
                 apply_action(game, seat, make(ActionKind::kEndTurn), rng) != Error::kNone) {
        (void)play_random_step(game, rng);
      }
    }
    note_new_pieces(before, game, built, memory);
    ++out.actions;
  }
  out.winner = game.winner;
  out.turns = game.turn_number;
  for (int s = 0; s < players; ++s) out.points[at(s)] = game.visible_points(s) + game.hidden_points(s);
  return out;
}

double ScoreStats::variance() const noexcept {
  if (n < 2) return 0.0;
  const double m = mean();
  return std::max(0.0, (sum_sq - static_cast<double>(n) * m * m) / static_cast<double>(n - 1));
}

double ScoreStats::std_error() const noexcept {
  return n > 1 ? std::sqrt(variance() / static_cast<double>(n)) : 0.0;
}

double Sprt::lower() const noexcept { return std::log(beta / (1 - alpha)); }
double Sprt::upper() const noexcept { return std::log((1 - beta) / alpha); }

double Sprt::llr(const ScoreStats& s) const noexcept {
  const double var = s.variance();
  if (s.n < 2 || var <= 0) return 0.0;
  return static_cast<double>(s.n) * (p1 - p0) * (2 * s.mean() - p0 - p1) / (2 * var);
}

SprtResult sprt_decide(const Sprt& test, const ScoreStats& s) noexcept {
  const double l = test.llr(s);
  if (l >= test.upper()) return SprtResult::kH1;
  if (l <= test.lower()) return SprtResult::kH0;
  return SprtResult::kContinue;
}

}  // namespace cae
