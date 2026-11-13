#pragma once

// The rules of catan-project (backend/app/models/game.py) in C++.
//
// One Game is the whole state of a game in a flat struct of fixed-size
// arrays and bitmasks: no pointers, no heap. Copying it is one memcpy of
// 768 bytes (sizeof(Game) with GCC on x86-64), which is what simulations
// will do millions of times.
//
// Every action is a member function that either changes the state and
// returns Error::kNone, or refuses and leaves the state exactly as it was.
// The checks run in the same order as in game.py, so the two engines refuse
// the same moves; tools/difftest.py holds them to that.
//
// Chance is an argument, not something the engine draws: roll_dice() takes
// the two dice and steal() takes the card. Replaying a record passes the
// recorded outcomes; a simulation will pass random ones. The engine itself is
// deterministic, so the same inputs always give the same game.

#include "catan/bits.hpp"
#include "catan/topology.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace cae {

inline constexpr int kMaxPlayers = 4;
inline constexpr int kResourceCount = 5;
inline constexpr int kDevCardKinds = 5;
inline constexpr int kMaxPorts = 9;
inline constexpr int kDevDeckSize = 25;
inline constexpr int kBankSize = 19;  // cards of each resource

enum class Resource : std::uint8_t { kWood, kBrick, kSheep, kWheat, kOre };
// Land types; the first five match Resource, so a non-desert hex produces
// Resource{terrain}.
enum class Terrain : std::uint8_t { kWood, kBrick, kSheep, kWheat, kOre, kDesert };
enum class DevCard : std::uint8_t { kKnight, kVictoryPoint, kRoadBuilding, kYearOfPlenty, kMonopoly };
// 3:1, or 2:1 for one resource.
enum class PortType : std::uint8_t { kAny, kWood, kBrick, kSheep, kWheat, kOre };

// What the current turn is waiting for (TurnPhase in game.py).
enum class Phase : std::uint8_t { kSetup, kRollDice, kDiscard, kMoveRobber, kSteal, kRoadBuilding, kMain };

// Cards per resource, indexed by Resource.
using Cards = std::array<int, kResourceCount>;

// Why an action was refused. Grouped roughly like the error messages of
// game.py; the difference test compares only "accepted or refused", because
// two refusals can have several reasons and the engines need not pick the
// same one.
enum class Error : std::uint8_t {
  kNone,
  kGameOver,
  kNotYourTurn,
  kWrongPhase,
  kBadArgument,       // unknown seat, resource or position
  kNotOnBoard,
  kOccupied,
  kDistanceRule,
  kNoPiecesLeft,
  kNotConnected,
  kMustTouchSetupSettlement,
  kNotEnoughResources,
  kNoSettlement,
  kAlreadyCity,
  kNothingToDiscard,
  kWrongDiscardCount,
  kRobberMustMove,
  kFriendlyRobber,
  kNoVictim,
  kBankEmpty,
  kSameResource,
  kBadTrade,
  kNoOffer,
  kNotAccepted,
  kDevCardAlreadyPlayed,
  kNoDevCard,
  kDevCardBoughtThisTurn,
  kDeckEmpty,
  kNoRoadSpace,
  kBadOutcome,        // recorded chance that cannot happen: die 7, card the victim lacks
};

[[nodiscard]] std::string_view error_name(Error e) noexcept;
[[nodiscard]] std::string_view phase_name(Phase p) noexcept;  // as in game.py, e.g. "main_phase"
[[nodiscard]] std::string_view resource_name(Resource r) noexcept;
[[nodiscard]] std::string_view dev_card_name(DevCard c) noexcept;

struct Settings {
  int victory_points = 10;
  int discard_limit = 7;      // on a 7, more cards than this means discarding half
  bool friendly_robber = false;
};

// The friendly robber spares opponents with this many visible points or fewer.
inline constexpr int kFriendlyRobberMaxPoints = 2;

struct Port {
  PortType type = PortType::kAny;
  NodeMask nodes = 0;  // the two corners that use it
};

struct Board {
  std::array<Terrain, kHexCount> terrain{};
  std::array<std::int8_t, kHexCount> number{};  // 2..12, 0 on the desert
  std::array<Port, kMaxPorts> ports{};
  int port_count = 0;
  // Hexes carrying each number, as bits over the 19 hexes: a roll of 8 looks
  // at hexes_by_number[8] only. Filled by index_numbers().
  std::array<std::uint32_t, 13> hexes_by_number{};

  void index_numbers() noexcept;
};

struct Player {
  Cards resources{};
  NodeMask settlements = 0;  // settlements and cities are disjoint sets
  NodeMask cities = 0;
  EdgeMask roads{};
  std::array<std::uint8_t, kDevCardKinds> dev_cards{};      // playable from this turn on
  std::array<std::uint8_t, kDevCardKinds> new_dev_cards{};  // bought this turn
  int knights = 0;

  [[nodiscard]] int card_count() const noexcept;
  [[nodiscard]] NodeMask buildings() const noexcept { return settlements | cities; }
  [[nodiscard]] bool has(const Cards& cost) const noexcept;
};

struct TradeOffer {
  bool open = false;
  std::int8_t proposer = -1;
  Cards give{};
  Cards get{};
  std::uint8_t accepted = 0;  // seats as bits
  std::uint8_t declined = 0;
};

struct Game {
  Board board;
  Settings settings;
  int player_count = 0;
  std::array<Player, kMaxPlayers> players{};

  int current = 0;      // whose turn
  int turn_number = 0;  // turns ended, setup included
  int dice_roll = 0;    // 0 until rolled this turn
  std::array<std::int8_t, 2> dice{};
  std::array<int, 11> roll_counts{};  // index 0 counts the 2s
  Phase phase = Phase::kSetup;

  // Setup goes round the table and back (snake order).
  std::array<std::int8_t, 2 * kMaxPlayers> setup_queue{};
  int setup_length = 0;
  int setup_position = 0;
  bool setup_waiting_for_road = false;
  int setup_last_node = -1;

  int robber_hex = -1;
  // Cards each seat still has to give up after a 7; -1 when it owes nothing.
  std::array<std::int8_t, kMaxPlayers> pending_discards{-1, -1, -1, -1};
  Phase return_phase = Phase::kMain;  // where the turn goes after the robber or free roads
  int free_roads_left = 0;
  TradeOffer offer;

  std::array<DevCard, kDevDeckSize> dev_deck{};  // in drawing order
  int dev_deck_size = 0;
  int dev_deck_next = 0;
  bool dev_card_played = false;

  int largest_army = -1;  // seat holding the award, or -1
  int longest_road = -1;
  std::array<int, kMaxPlayers> road_lengths{};

  bool game_over = false;
  int winner = -1;

  // A new game in setup. `deck` lists the development cards in the order they
  // will be drawn. The robber starts on the first desert, as in game.py.
  [[nodiscard]] static Game create(const Board& board, const Settings& settings, int players,
                                   int first_seat, std::span<const DevCard> deck);

  // --- Actions (one per client action in catan-project) ---
  Error build_settlement(int seat, int node);
  Error build_road(int seat, int edge);
  Error upgrade_city(int seat, int node);
  Error roll_dice(int seat, int die1, int die2);
  Error discard(int seat, const Cards& cards);
  Error move_robber(int seat, int hex);
  Error steal(int seat, int victim, Resource taken);
  Error trade_with_bank(int seat, Resource give, Resource get);
  Error offer_trade(int seat, const Cards& give, const Cards& get);
  Error respond_trade(int seat, bool accept);
  Error confirm_trade(int seat, int partner);
  Error cancel_trade(int seat);
  Error buy_dev_card(int seat);
  Error play_knight(int seat);
  Error play_road_building(int seat);
  Error play_year_of_plenty(int seat, Resource first, Resource second);
  Error play_monopoly(int seat, Resource resource);
  Error end_turn(int seat);

  // --- Legal moves (empty unless it is that seat's move) ---
  [[nodiscard]] NodeMask legal_settlements(int seat) const;
  [[nodiscard]] EdgeMask legal_roads(int seat) const;
  [[nodiscard]] NodeMask legal_cities(int seat) const;
  [[nodiscard]] std::uint32_t legal_robber_hexes(int seat) const;  // bits over the 19 hexes
  [[nodiscard]] std::uint8_t steal_victims(int thief) const;       // bits over seats
  [[nodiscard]] std::uint8_t playable_dev_cards(int seat) const;   // bits over DevCard
  [[nodiscard]] bool can_buy_dev_card(int seat) const;

  // --- Derived facts ---
  [[nodiscard]] int visible_points(int seat) const noexcept;
  [[nodiscard]] int hidden_points(int seat) const noexcept;  // victory point cards in hand
  [[nodiscard]] int bank_count(Resource r) const noexcept;
  [[nodiscard]] int trade_rate(int seat, Resource give) const noexcept;
  [[nodiscard]] int road_length(int seat) const;

 private:
  [[nodiscard]] bool valid_seat(int seat) const noexcept { return seat >= 0 && seat < player_count; }
  [[nodiscard]] NodeMask occupied() const noexcept;
  [[nodiscard]] Error verify_turn(int seat, Phase expected) const noexcept;
  [[nodiscard]] Error check_settlement_spot(int seat, int node, bool free) const noexcept;
  [[nodiscard]] Error check_road_spot(int seat, int edge) const noexcept;
  [[nodiscard]] EdgeMask free_road_edges(int seat) const;
  [[nodiscard]] bool is_protected(int thief, int seat) const noexcept;
  [[nodiscard]] std::uint8_t owners_around(int hex) const noexcept;
  [[nodiscard]] Error spend_dev_card(int seat, DevCard card);

  void produce(int roll);
  void give_initial_resources(int seat, int node);
  void start_robber();
  void next_turn();
  void check_victory();
  void update_longest_road(std::uint8_t changed);  // seats as bits
  void update_largest_army(int seat);
  void finish_road_building_if_done(int seat);
};

// Building costs, indexed by Resource: wood, brick, sheep, wheat, ore.
inline constexpr Cards kRoadCost = {1, 1, 0, 0, 0};
inline constexpr Cards kSettlementCost = {1, 1, 1, 1, 0};
inline constexpr Cards kCityCost = {0, 0, 0, 2, 3};
inline constexpr Cards kDevCardCost = {0, 0, 1, 1, 1};

inline constexpr int kMaxRoads = 15;
inline constexpr int kMaxSettlements = 5;
inline constexpr int kMaxCities = 4;

}  // namespace cae
