#pragma once

// The bot zoo of catan-project in C++: the hard bot and its variants.
//
// Simulations (step 10 of the plan) play a position out to the end many
// times, and the opponents in those games must play the way different
// people do. catan-project already has such bots, written in Go
// (gateway/internal/bot: hard.go, hard_moves.go, zoo.go, strategy.go and
// docs/bot-zoo.md there). This is a port of them, decision for decision:
// given the same position a C++ zoo bot makes exactly the move the Go one
// makes. tools/zoo_check.cpp holds the two to that on recorded games.
//
// A zoo bot is the hard level with
//  - an archetype: what it builds toward (weights on resources, ports,
//    variety, each goal, and how far behind the Longest Road it still
//    builds for it);
//  - a personality: how it trades with players (offers per turn, which
//    offers it takes, whom it refuses);
//  - a seed that shakes every weight by its own random factor around 1.
//
// The bot sees only what its seat sees on the platform: its own hand, the
// board, everyone's visible points, card counts, knights and road lengths.
// It never reads another seat's cards or the development card deck.

#include "catan/game.hpp"
#include "catan/rng.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cae {

// --- Actions ---------------------------------------------------------------

// One move, as the platform's client sends it. Named like the actions in
// game records.
enum class ActionKind : std::uint8_t {
  kBuildSettlement,
  kBuildRoad,
  kUpgradeCity,
  kRollDice,
  kDiscard,
  kMoveRobber,
  kSteal,
  kTradeWithBank,
  kOfferTrade,
  kRespondTrade,
  kConfirmTrade,
  kCancelTrade,
  kBuyDevCard,
  kPlayKnight,
  kPlayRoadBuilding,
  kPlayYearOfPlenty,
  kPlayMonopoly,
  kEndTurn,
};

[[nodiscard]] std::string_view action_name(ActionKind kind) noexcept;

struct Action {
  ActionKind kind = ActionKind::kEndTurn;
  // The node, edge or hex to build on or move to, the victim of a steal or
  // the partner of a confirmed trade; -1 when the action takes none.
  int target = -1;
  // Bank trade: give `first`, get `second`. Year of plenty: both cards.
  // Monopoly: `first`.
  Resource first = Resource::kWood;
  Resource second = Resource::kWood;
  Cards give{};  // discard (the cards), offer_trade
  Cards get{};   // offer_trade
  bool accept = false;  // respond_trade

  friend bool operator==(const Action&, const Action&) = default;
};

// Plays `action` for `seat`. Chance comes from `rng`: the dice of a roll and
// the card a steal takes (each card in the victim's hand equally likely, as
// on the platform).
Error apply_action(Game& game, int seat, const Action& action, Rng& rng);

// --- Zoo bots --------------------------------------------------------------

enum class Archetype : std::uint8_t { kBalanced, kOreWheat, kWoodBrick, kPort };
enum class Personality : std::uint8_t { kFair, kFriendly, kWary, kLoner };
inline constexpr int kArchetypeCount = 4;
inline constexpr int kPersonalityCount = 4;

[[nodiscard]] std::string_view archetype_name(Archetype a) noexcept;      // "ore-wheat"
[[nodiscard]] std::string_view personality_name(Personality p) noexcept;  // "friendly"

// What the hard level values. At 1 a weight leaves the hard level as it is.
struct ZooWeights {
  std::array<double, kResourceCount> resource{1, 1, 1, 1, 1};  // pips of each resource in a spot
  double variety = 1;     // bonus for a new resource, discount for more of one
  double port = 1;        // bonus of a port the bot would use
  double city = 1;        // what each goal is worth
  double settlement = 1;
  double dev_card = 1;
  int road_reach = 2;     // roads behind the Longest Road it still builds for it
  // Extra weight of the leader when choosing the robber's hex: the leader's
  // buildings count (1 + leader_focus) times. 0 is the hard level (and the
  // Go bots, which have no such weight); MAP-Elites (map_elites.hpp) tunes it.
  double leader_focus = 0;
};

// How a bot trades with players.
struct ZooTrading {
  int offers_per_turn = 2;
  bool accepts = true;   // answers yes to anything at all
  bool neutral = false;  // takes a trade that leaves its goal no further away
  // 0 trades with anyone; 1 refuses the leader and anyone close to winning;
  // 2 also refuses anyone ahead of itself and never confirms its own offer
  // with them
  int embargo = 1;
};

struct ZooBot {
  Archetype archetype = Archetype::kBalanced;
  Personality personality = Personality::kFair;
  std::uint64_t seed = 0;
  ZooWeights weights;
  ZooTrading trading;
};

// The weights of an archetype and the trading of a personality, before any
// shaking. Archetype balanced with personality fair is the hard level.
[[nodiscard]] ZooWeights archetype_weights(Archetype a) noexcept;
[[nodiscard]] ZooTrading personality_trading(Personality p) noexcept;

// A zoo bot with every weight multiplied by exp(0.2 z), z standard normal,
// drawn from `seed`; seed 0 shakes nothing. The factors are drawn with our
// own Rng, so they differ from the Go bot of the same seed (records carry
// the shaken weights, so a recorded bot is rebuilt from those instead), but
// the spread is the same. Only + - * / go into them, so the native and
// WebAssembly builds get the same bot from a seed.
[[nodiscard]] ZooBot make_zoo_bot(Archetype a, Personality p, std::uint64_t seed);

// The hard level: zoo balanced, fair, no shaking.
[[nodiscard]] ZooBot hard_bot();

// A bot drawn like a `zoo` seat of the platform's tournament: archetype,
// personality and weight seed uniformly at random.
[[nodiscard]] ZooBot random_zoo_bot(Rng& rng);

// What a bot on the platform knows that the Game struct does not keep:
//  - how many trades the current player has offered this turn (the Go bot
//    counts its own lines in the game log);
//  - in which order players accepted the open offer (it confirms the first);
//  - in which order the settlements were built: the server lists the spots
//    a city may go in that order, and of two equally good spots the bot
//    takes the first.
// play_zoo_step() keeps it; tools/zoo_check.cpp rebuilds it from a record.
struct TableMemory {
  int turn = -1;  // Game::turn_number the turn fields are about
  int offers_made = 0;
  std::array<std::int8_t, kMaxPlayers> accepted{-1, -1, -1, -1};
  int accepted_count = 0;
  // Every settlement ever built, in order (a city stays where it was).
  std::array<std::int8_t, kMaxPlayers * kMaxSettlements * 2> built{};
  int built_count = 0;

  // Brings the memory up to the game: forgets the turn when a new one began.
  void sync(const Game& game) noexcept;
  // Notes an action that was just played (call sync() first).
  void record(int seat, const Action& action) noexcept;
};

// The bot's next move for `seat`, or nothing when it has nothing to do (not
// its move, or its own offer is still waiting for answers). Same order of
// questions as Decide() in strategy.go: a discard it owes, then an offer to
// answer, then its own turn.
[[nodiscard]] std::optional<Action> zoo_decide(const Game& game, int seat, const ZooBot& bot,
                                               const TableMemory& memory);

// --- Games between zoo bots ------------------------------------------------

// Who must act now, the way the platform's bots take turns: a seat that owes
// a discard (lowest first), then players answering an open offer (lowest
// seat first), then the current player. -1 when the game is over.
[[nodiscard]] int next_actor(const Game& game);

struct ZooTable {
  Game game;
  std::array<ZooBot, kMaxPlayers> bots{};
  TableMemory memory;
};

struct ZooStepResult {
  Error error = Error::kNone;  // kGameOver once the game has ended
  bool fallback = false;       // the bot's move was refused and a safe one played
};

// One move of whoever must act. If the engine refuses the bot's move, the
// seat does what the Go bot does after a refusal: withdraws its own offer or
// ends its turn (or, outside the main phase, a random legal move).
ZooStepResult play_zoo_step(ZooTable& table, Rng& rng);

struct ZooPlayout {
  int winner = -1;  // -1 when max_actions ran out first
  int actions = 0;
  int fallbacks = 0;  // refused bot moves; 0 unless the port is broken
};

// Plays until the game ends or max_actions moves have been made.
ZooPlayout play_out_zoo(ZooTable& table, Rng& rng, int max_actions = 40'000);

// --- Orders the platform lists things in -------------------------------------

// catan-project lists legal settlement spots and roads in the iteration
// order of a Python set of positions, and the hard bot takes the first of
// equally good options. Python's order is fixed (the hashes of small int
// tuples do not depend on PYTHONHASHSEED), so it is a table here.
[[nodiscard]] const std::array<std::int8_t, kNodeCount>& server_node_order() noexcept;
[[nodiscard]] const std::array<std::int8_t, kEdgeCount>& server_edge_order() noexcept;

}  // namespace cae
