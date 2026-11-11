#include "catan/game.hpp"

#include <algorithm>
#include <bit>

namespace cae {

namespace {

// Array index from an int that is known to be in range. Spelled out once so
// -Wconversion stays quiet without casts on every line.
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }
constexpr std::size_t at(Resource r) noexcept { return static_cast<std::size_t>(r); }
constexpr std::size_t at(DevCard c) noexcept { return static_cast<std::size_t>(c); }

constexpr std::uint8_t seat_bit(int seat) noexcept { return static_cast<std::uint8_t>(1U << seat); }
constexpr std::uint32_t hex_bit(int hex) noexcept { return std::uint32_t{1} << hex; }
constexpr std::uint32_t kAllHexes = (std::uint32_t{1} << kHexCount) - 1;

constexpr int kRoadBuildingRoads = 2;
constexpr int kLongestRoadMin = 5;
constexpr int kLargestArmyMin = 3;
constexpr int kAwardPoints = 2;

void add(Cards& hand, const Cards& cards) noexcept {
  for (std::size_t r = 0; r < kResourceCount; ++r) hand[r] += cards[r];
}
void subtract(Cards& hand, const Cards& cards) noexcept {
  for (std::size_t r = 0; r < kResourceCount; ++r) hand[r] -= cards[r];
}
bool any_card(const Cards& cards) noexcept {
  return std::any_of(cards.begin(), cards.end(), [](int n) { return n != 0; });
}

}  // namespace

std::string_view error_name(Error e) noexcept {
  switch (e) {
    case Error::kNone: return "none";
    case Error::kGameOver: return "game_over";
    case Error::kNotYourTurn: return "not_your_turn";
    case Error::kWrongPhase: return "wrong_phase";
    case Error::kBadArgument: return "bad_argument";
    case Error::kNotOnBoard: return "not_on_board";
    case Error::kOccupied: return "occupied";
    case Error::kDistanceRule: return "distance_rule";
    case Error::kNoPiecesLeft: return "no_pieces_left";
    case Error::kNotConnected: return "not_connected";
    case Error::kMustTouchSetupSettlement: return "must_touch_setup_settlement";
    case Error::kNotEnoughResources: return "not_enough_resources";
    case Error::kNoSettlement: return "no_settlement";
    case Error::kAlreadyCity: return "already_city";
    case Error::kNothingToDiscard: return "nothing_to_discard";
    case Error::kWrongDiscardCount: return "wrong_discard_count";
    case Error::kRobberMustMove: return "robber_must_move";
    case Error::kFriendlyRobber: return "friendly_robber";
    case Error::kNoVictim: return "no_victim";
    case Error::kBankEmpty: return "bank_empty";
    case Error::kSameResource: return "same_resource";
    case Error::kBadTrade: return "bad_trade";
    case Error::kNoOffer: return "no_offer";
    case Error::kNotAccepted: return "not_accepted";
    case Error::kDevCardAlreadyPlayed: return "dev_card_already_played";
    case Error::kNoDevCard: return "no_dev_card";
    case Error::kDevCardBoughtThisTurn: return "dev_card_bought_this_turn";
    case Error::kDeckEmpty: return "deck_empty";
    case Error::kNoRoadSpace: return "no_road_space";
    case Error::kBadOutcome: return "bad_outcome";
  }
  return "unknown";
}

std::string_view phase_name(Phase p) noexcept {
  switch (p) {
    case Phase::kSetup: return "setup";
    case Phase::kRollDice: return "roll_dice";
    case Phase::kDiscard: return "discard";
    case Phase::kMoveRobber: return "move_robber";
    case Phase::kSteal: return "steal";
    case Phase::kRoadBuilding: return "road_building";
    case Phase::kMain: return "main_phase";
  }
  return "unknown";
}

std::string_view resource_name(Resource r) noexcept {
  switch (r) {
    case Resource::kWood: return "wood";
    case Resource::kBrick: return "brick";
    case Resource::kSheep: return "sheep";
    case Resource::kWheat: return "wheat";
    case Resource::kOre: return "ore";
  }
  return "unknown";
}

std::string_view dev_card_name(DevCard c) noexcept {
  switch (c) {
    case DevCard::kKnight: return "knight";
    case DevCard::kVictoryPoint: return "victory_point";
    case DevCard::kRoadBuilding: return "road_building";
    case DevCard::kYearOfPlenty: return "year_of_plenty";
    case DevCard::kMonopoly: return "monopoly";
  }
  return "unknown";
}

void Board::index_numbers() noexcept {
  hexes_by_number.fill(0);
  for (int h = 0; h < kHexCount; ++h) {
    const int n = number[at(h)];
    if (terrain[at(h)] != Terrain::kDesert && n >= 2 && n <= 12) hexes_by_number[at(n)] |= hex_bit(h);
  }
}

int Player::card_count() const noexcept {
  int total = 0;
  for (int n : resources) total += n;
  return total;
}

bool Player::has(const Cards& cost) const noexcept {
  for (std::size_t r = 0; r < kResourceCount; ++r) {
    if (resources[r] < cost[r]) return false;
  }
  return true;
}

Game Game::create(const Board& board, const Settings& settings, int players, int first_seat,
                  std::span<const DevCard> deck) {
  Game g;
  // Precondition: 2..4 players (the record reader checks). The clamp also
  // tells the compiler the queue writes below stay inside the array.
  players = std::clamp(players, 2, kMaxPlayers);
  g.board = board;
  g.board.index_numbers();
  g.settings = settings;
  g.player_count = players;
  // Snake order from the first seat, e.g. 1 2 3 0 0 3 2 1. The first seat
  // places last and so also takes the first regular turn.
  for (int i = 0; i < players; ++i) {
    const auto seat = static_cast<std::int8_t>((first_seat + i) % players);
    g.setup_queue[at(i)] = seat;
    g.setup_queue[at(2 * players - 1 - i)] = seat;
  }
  g.setup_length = 2 * players;
  g.current = g.setup_queue[0];
  g.robber_hex = hex_at(0, 0);
  for (int h = 0; h < kHexCount; ++h) {
    if (board.terrain[at(h)] == Terrain::kDesert) {
      g.robber_hex = h;
      break;
    }
  }
  g.dev_deck_size = static_cast<int>(std::min<std::size_t>(deck.size(), kDevDeckSize));
  std::copy_n(deck.begin(), g.dev_deck_size, g.dev_deck.begin());
  return g;
}

// --- Derived facts ---

int Game::visible_points(int seat) const noexcept {
  const Player& p = players[at(seat)];
  return count(p.settlements) + 2 * count(p.cities) + (longest_road == seat ? kAwardPoints : 0) +
         (largest_army == seat ? kAwardPoints : 0);
}

int Game::hidden_points(int seat) const noexcept {
  const Player& p = players[at(seat)];
  return p.dev_cards[at(DevCard::kVictoryPoint)] + p.new_dev_cards[at(DevCard::kVictoryPoint)];
}

int Game::bank_count(Resource r) const noexcept {
  int held = 0;
  for (int s = 0; s < player_count; ++s) held += players[at(s)].resources[at(r)];
  return std::max(kBankSize - held, 0);
}

int Game::trade_rate(int seat, Resource give) const noexcept {
  const NodeMask mine = players[at(seat)].buildings();
  bool any_port = false;
  for (int i = 0; i < board.port_count; ++i) {
    const Port& port = board.ports[at(i)];
    if ((port.nodes & mine) == 0) continue;
    if (port.type == PortType::kAny) {
      any_port = true;
    } else if (static_cast<int>(port.type) - 1 == static_cast<int>(give)) {
      return 2;
    }
  }
  return any_port ? 3 : 4;
}

NodeMask Game::occupied() const noexcept {
  NodeMask all = 0;
  for (int s = 0; s < player_count; ++s) all |= players[at(s)].buildings();
  return all;
}

// The longest trail of the seat's roads leaving `node`, each road used once.
// An opponent's building stops a trail that reaches it, but a trail may start
// there (game.py: `if used and building and building.owner != color`).
int Game::longest_trail_from(int node, int seat, EdgeMask used, bool started) const {
  if (started && contains(occupied() & ~players[at(seat)].buildings(), node)) return 0;
  int best = 0;
  const EdgeMask& roads = players[at(seat)].roads;
  for (const std::int8_t e : kTopology.node_edges[at(node)]) {
    if (e < 0 || !roads.test(e) || used.test(e)) continue;
    const auto& ends = kTopology.edge_nodes[at(e)];
    const int other = ends[0] == node ? ends[1] : ends[0];
    EdgeMask next = used;
    next.set(e);
    best = std::max(best, 1 + longest_trail_from(other, seat, next, true));
  }
  return best;
}

int Game::road_length(int seat) const {
  int best = 0;
  for_each_edge(players[at(seat)].roads, [&](int e) {
    for (const std::int8_t node : kTopology.edge_nodes[at(e)]) {
      best = std::max(best, longest_trail_from(node, seat, EdgeMask{}, false));
    }
  });
  return best;
}

// --- Checks shared by actions and legal-move queries ---

Error Game::verify_turn(int seat, Phase expected) const noexcept {
  if (game_over) return Error::kGameOver;
  if (seat != current) return Error::kNotYourTurn;
  if (phase != expected) return Error::kWrongPhase;
  return Error::kNone;
}

Error Game::check_settlement_spot(int seat, int node, bool free) const noexcept {
  if (node < 0) return Error::kNotOnBoard;
  const NodeMask taken = occupied();
  if (contains(taken, node)) return Error::kOccupied;
  if ((taken & kTopology.node_neighbor_mask[at(node)]) != 0) return Error::kDistanceRule;
  const Player& p = players[at(seat)];
  if (count(p.settlements) >= kMaxSettlements) return Error::kNoPiecesLeft;
  if (!free && !(p.roads & kTopology.node_edge_mask[at(node)]).any()) return Error::kNotConnected;
  return Error::kNone;
}

Error Game::check_road_spot(int seat, int edge) const noexcept {
  if (edge < 0) return Error::kNotOnBoard;
  for (int s = 0; s < player_count; ++s) {
    if (players[at(s)].roads.test(edge)) return Error::kOccupied;
  }
  const Player& p = players[at(seat)];
  if (p.roads.count() >= kMaxRoads) return Error::kNoPiecesLeft;
  const auto& ends = kTopology.edge_nodes[at(edge)];
  if (phase == Phase::kSetup && setup_waiting_for_road && ends[0] != setup_last_node &&
      ends[1] != setup_last_node) {
    return Error::kMustTouchSetupSettlement;
  }
  // Connected through either end: an own building there, or an own road
  // there that is not blocked by an opponent's building on that corner.
  const NodeMask taken = occupied();
  for (const std::int8_t node : ends) {
    if (contains(taken, node)) {
      if (contains(p.buildings(), node)) return Error::kNone;
      continue;
    }
    if ((p.roads & kTopology.node_edge_mask[at(node)]).any()) return Error::kNone;
  }
  return Error::kNotConnected;
}

EdgeMask Game::free_road_edges(int seat) const {
  EdgeMask out;
  for (int e = 0; e < kEdgeCount; ++e) {
    if (check_road_spot(seat, e) == Error::kNone) out.set(e);
  }
  return out;
}

bool Game::is_protected(int thief, int seat) const noexcept {
  return settings.friendly_robber && seat != thief && visible_points(seat) <= kFriendlyRobberMaxPoints;
}

std::uint8_t Game::owners_around(int hex) const noexcept {
  std::uint8_t owners = 0;
  for (int s = 0; s < player_count; ++s) {
    if ((players[at(s)].buildings() & kTopology.hex_node_mask[at(hex)]) != 0) owners |= seat_bit(s);
  }
  return owners;
}

// --- Turn flow ---

void Game::check_victory() {
  if (!game_over && visible_points(current) + hidden_points(current) >= settings.victory_points) {
    game_over = true;
    winner = current;
  }
}

void Game::next_turn() {
  check_victory();
  if (game_over) return;
  ++turn_number;
  if (phase == Phase::kSetup) {
    ++setup_position;
    setup_waiting_for_road = false;
    setup_last_node = -1;
    if (setup_position == setup_length) {
      phase = Phase::kRollDice;  // the last to place keeps the turn
      dice_roll = 0;
    } else {
      current = setup_queue[at(setup_position)];
    }
    return;
  }
  offer = TradeOffer{};
  Player& finished = players[at(current)];
  for (std::size_t c = 0; c < kDevCardKinds; ++c) {
    finished.dev_cards[c] = static_cast<std::uint8_t>(finished.dev_cards[c] + finished.new_dev_cards[c]);
    finished.new_dev_cards[c] = 0;
  }
  dev_card_played = false;
  current = (current + 1) % player_count;
  dice_roll = 0;
  phase = Phase::kRollDice;
  // Points gained outside one's turn (Longest Road) only win on one's own turn.
  check_victory();
}

Error Game::end_turn(int seat) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  next_turn();
  return Error::kNone;
}

// --- Dice, production, robber ---

void Game::produce(int roll) {
  // shares[resource][seat]: cards owed. popcount(buildings & corners of the
  // hex) counts a player's buildings on a hex in one instruction.
  std::array<std::array<int, kMaxPlayers>, kResourceCount> shares{};
  std::uint32_t hexes = board.hexes_by_number[at(roll)] & ~hex_bit(robber_hex);
  while (hexes != 0) {
    const int h = std::countr_zero(hexes);
    hexes &= hexes - 1;
    const NodeMask corners = kTopology.hex_node_mask[at(h)];
    const std::size_t r = static_cast<std::size_t>(board.terrain[at(h)]);
    for (int s = 0; s < player_count; ++s) {
      const Player& p = players[at(s)];
      shares[r][at(s)] += count(p.settlements & corners) + 2 * count(p.cities & corners);
    }
  }
  for (std::size_t r = 0; r < kResourceCount; ++r) {
    int total = 0;
    int receivers = 0;
    for (int s = 0; s < player_count; ++s) {
      total += shares[r][at(s)];
      receivers += shares[r][at(s)] > 0 ? 1 : 0;
    }
    if (receivers == 0) continue;
    // When the bank cannot pay everyone, nobody gets that resource; a single
    // receiver still takes whatever is left.
    const int available = bank_count(static_cast<Resource>(r));
    if (total > available && receivers > 1) continue;
    for (int s = 0; s < player_count; ++s) {
      players[at(s)].resources[r] += std::min(shares[r][at(s)], available);
    }
  }
}

void Game::start_robber() {
  bool any = false;
  for (int s = 0; s < player_count; ++s) {
    const int cards = players[at(s)].card_count();
    pending_discards[at(s)] = static_cast<std::int8_t>(cards > settings.discard_limit ? cards / 2 : -1);
    any = any || cards > settings.discard_limit;
  }
  phase = any ? Phase::kDiscard : Phase::kMoveRobber;
}

Error Game::roll_dice(int seat, int die1, int die2) {
  if (game_over) return Error::kGameOver;
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (seat != current) return Error::kNotYourTurn;
  if (phase != Phase::kRollDice) return Error::kWrongPhase;
  if (die1 < 1 || die1 > 6 || die2 < 1 || die2 > 6) return Error::kBadOutcome;
  dice = {static_cast<std::int8_t>(die1), static_cast<std::int8_t>(die2)};
  dice_roll = die1 + die2;
  ++roll_counts[at(dice_roll - 2)];
  if (dice_roll == 7) {
    return_phase = Phase::kMain;
    start_robber();
  } else {
    phase = Phase::kMain;
    produce(dice_roll);
  }
  return Error::kNone;
}

Error Game::discard(int seat, const Cards& cards) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (game_over) return Error::kGameOver;
  if (phase != Phase::kDiscard) return Error::kNothingToDiscard;
  const int required = pending_discards[at(seat)];
  if (required <= 0) return Error::kNothingToDiscard;
  int total = 0;
  for (int n : cards) {
    if (n < 0) return Error::kBadArgument;
    total += n;
  }
  if (total != required) return Error::kWrongDiscardCount;
  Player& p = players[at(seat)];
  if (!p.has(cards)) return Error::kNotEnoughResources;
  subtract(p.resources, cards);
  pending_discards[at(seat)] = -1;
  if (std::all_of(pending_discards.begin(), pending_discards.end(), [](std::int8_t n) { return n < 0; })) {
    phase = Phase::kMoveRobber;
  }
  return Error::kNone;
}

std::uint32_t Game::legal_robber_hexes(int seat) const {
  if (!valid_seat(seat) || game_over || seat != current || phase != Phase::kMoveRobber) return 0;
  const std::uint32_t hexes = kAllHexes & ~hex_bit(robber_hex);
  if (!settings.friendly_robber) return hexes;
  std::uint32_t allowed = 0;
  for (int h = 0; h < kHexCount; ++h) {
    if ((hexes & hex_bit(h)) == 0) continue;
    bool spares = board.terrain[at(h)] == Terrain::kDesert;
    if (!spares) {
      spares = true;
      const std::uint8_t owners = owners_around(h);
      for (int s = 0; s < player_count; ++s) {
        if ((owners & seat_bit(s)) != 0 && is_protected(seat, s)) spares = false;
      }
    }
    if (spares) allowed |= hex_bit(h);
  }
  // When everyone is protected the robber still has to move somewhere.
  return allowed != 0 ? allowed : hexes;
}

std::uint8_t Game::steal_victims(int thief) const {
  std::uint8_t victims = 0;
  const std::uint8_t owners = owners_around(robber_hex);
  for (int s = 0; s < player_count; ++s) {
    if (s != thief && players[at(s)].card_count() > 0 && (owners & seat_bit(s)) != 0 &&
        !is_protected(thief, s)) {
      victims |= seat_bit(s);
    }
  }
  return victims;
}

Error Game::move_robber(int seat, int hex) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMoveRobber); e != Error::kNone) return e;
  if (hex == robber_hex) return Error::kRobberMustMove;
  if (hex < 0) return Error::kNotOnBoard;
  if ((legal_robber_hexes(seat) & hex_bit(hex)) == 0) return Error::kFriendlyRobber;
  robber_hex = hex;
  phase = steal_victims(seat) != 0 ? Phase::kSteal : return_phase;
  return Error::kNone;
}

Error Game::steal(int seat, int victim, Resource taken) {
  if (!valid_seat(seat) || !valid_seat(victim)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kSteal); e != Error::kNone) return e;
  if (seat == victim) return Error::kNoVictim;
  if ((owners_around(robber_hex) & seat_bit(victim)) == 0) return Error::kNoVictim;
  Player& v = players[at(victim)];
  if (v.card_count() == 0) return Error::kNoVictim;
  if (is_protected(seat, victim)) return Error::kFriendlyRobber;
  if (v.resources[at(taken)] == 0) return Error::kBadOutcome;
  --v.resources[at(taken)];
  ++players[at(seat)].resources[at(taken)];
  phase = return_phase;
  return Error::kNone;
}

// --- Building ---

void Game::give_initial_resources(int seat, int node) {
  for (const std::int8_t h : kTopology.node_hexes[at(node)]) {
    if (h < 0 || board.terrain[at(h)] == Terrain::kDesert) continue;
    const auto r = static_cast<Resource>(board.terrain[at(h)]);
    if (bank_count(r) > 0) ++players[at(seat)].resources[at(r)];
  }
}

Error Game::build_settlement(int seat, int node) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (game_over) return Error::kGameOver;
  bool free = false;
  if (phase == Phase::kSetup) {
    if (seat != current) return Error::kNotYourTurn;
    if (setup_waiting_for_road) return Error::kWrongPhase;
    free = true;
  } else if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) {
    return e;
  }
  if (Error e = check_settlement_spot(seat, node, free); e != Error::kNone) return e;
  Player& p = players[at(seat)];
  if (!free && !p.has(kSettlementCost)) return Error::kNotEnoughResources;
  if (!free) subtract(p.resources, kSettlementCost);
  p.settlements |= bit(node);
  if (phase == Phase::kSetup) {
    if (count(p.buildings()) == 2) give_initial_resources(seat, node);
    setup_waiting_for_road = true;
    setup_last_node = node;
  }
  // A new settlement may cut an opponent's road in two.
  update_longest_road();
  check_victory();
  return Error::kNone;
}

void Game::finish_road_building_if_done(int seat) {
  if (free_roads_left == 0 || !free_road_edges(seat).any()) {
    free_roads_left = 0;
    phase = return_phase;
  }
}

Error Game::build_road(int seat, int edge) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (game_over) return Error::kGameOver;
  bool free = false;
  if (phase == Phase::kSetup) {
    if (seat != current) return Error::kNotYourTurn;
    if (!setup_waiting_for_road) return Error::kWrongPhase;
    free = true;
  } else if (phase == Phase::kRoadBuilding) {
    if (Error e = verify_turn(seat, Phase::kRoadBuilding); e != Error::kNone) return e;
    free = true;
  } else if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) {
    return e;
  }
  if (Error e = check_road_spot(seat, edge); e != Error::kNone) return e;
  Player& p = players[at(seat)];
  if (!free && !p.has(kRoadCost)) return Error::kNotEnoughResources;
  if (!free) subtract(p.resources, kRoadCost);
  p.roads.set(edge);
  update_longest_road();
  if (phase == Phase::kSetup) {
    next_turn();
    return Error::kNone;
  }
  if (phase == Phase::kRoadBuilding) {
    --free_roads_left;
    finish_road_building_if_done(seat);
  }
  check_victory();
  return Error::kNone;
}

Error Game::upgrade_city(int seat, int node) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  if (node < 0 || !contains(occupied(), node)) return Error::kNoSettlement;
  Player& p = players[at(seat)];
  if (!contains(p.buildings(), node)) return Error::kNoSettlement;
  if (contains(p.cities, node)) return Error::kAlreadyCity;
  if (count(p.cities) >= kMaxCities) return Error::kNoPiecesLeft;
  if (!p.has(kCityCost)) return Error::kNotEnoughResources;
  subtract(p.resources, kCityCost);
  p.settlements &= ~bit(node);
  p.cities |= bit(node);
  check_victory();
  return Error::kNone;
}

// --- Awards ---

void Game::update_longest_road() {
  int best = 0;
  for (int s = 0; s < player_count; ++s) {
    road_lengths[at(s)] = road_length(s);
    best = std::max(best, road_lengths[at(s)]);
  }
  // The holder keeps it on a tie; if a broken road leaves several others
  // tied for first, the award is set aside until one of them leads.
  if (best < kLongestRoadMin) {
    longest_road = -1;
  } else if (longest_road >= 0 && road_lengths[at(longest_road)] == best) {
    // unchanged
  } else {
    int leader = -1;
    int leaders = 0;
    for (int s = 0; s < player_count; ++s) {
      if (road_lengths[at(s)] == best) {
        leader = s;
        ++leaders;
      }
    }
    longest_road = leaders == 1 ? leader : -1;
  }
}

void Game::update_largest_army(int seat) {
  // The first to 3 knights holds it; a challenger must have strictly more.
  if (largest_army == seat || players[at(seat)].knights < kLargestArmyMin) return;
  if (largest_army >= 0 && players[at(seat)].knights <= players[at(largest_army)].knights) return;
  largest_army = seat;
}

// --- Trading ---

Error Game::trade_with_bank(int seat, Resource give, Resource get) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  if (give == get) return Error::kSameResource;
  const int cost = trade_rate(seat, give);
  Player& p = players[at(seat)];
  if (p.resources[at(give)] < cost) return Error::kNotEnoughResources;
  if (bank_count(get) == 0) return Error::kBankEmpty;
  p.resources[at(give)] -= cost;
  ++p.resources[at(get)];
  return Error::kNone;
}

Error Game::offer_trade(int seat, const Cards& give, const Cards& get) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  if (!any_card(give) || !any_card(get)) return Error::kBadTrade;
  for (std::size_t r = 0; r < kResourceCount; ++r) {
    if (give[r] < 0 || get[r] < 0) return Error::kBadTrade;
  }
  for (std::size_t r = 0; r < kResourceCount; ++r) {
    if (give[r] > 0 && get[r] > 0) return Error::kBadTrade;
  }
  if (!players[at(seat)].has(give)) return Error::kNotEnoughResources;
  offer = TradeOffer{true, static_cast<std::int8_t>(seat), give, get, 0, 0};
  return Error::kNone;
}

Error Game::respond_trade(int seat, bool accept) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (game_over) return Error::kGameOver;
  if (!offer.open) return Error::kNoOffer;
  if (seat == offer.proposer) return Error::kBadTrade;
  if (accept && !players[at(seat)].has(offer.get)) return Error::kNotEnoughResources;
  const auto keep = static_cast<std::uint8_t>(~seat_bit(seat));
  offer.accepted &= keep;
  offer.declined &= keep;
  (accept ? offer.accepted : offer.declined) |= seat_bit(seat);
  return Error::kNone;
}

Error Game::confirm_trade(int seat, int partner) {
  if (!valid_seat(seat) || !valid_seat(partner)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  if (!offer.open) return Error::kNoOffer;
  if ((offer.accepted & seat_bit(partner)) == 0) return Error::kNotAccepted;
  Player& me = players[at(seat)];
  Player& other = players[at(partner)];
  // The offer may be old: both hands could have changed since it was made.
  if (!me.has(offer.give) || !other.has(offer.get)) return Error::kNotEnoughResources;
  subtract(me.resources, offer.give);
  subtract(other.resources, offer.get);
  add(other.resources, offer.give);
  add(me.resources, offer.get);
  offer = TradeOffer{};
  return Error::kNone;
}

Error Game::cancel_trade(int seat) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (game_over) return Error::kGameOver;
  if (!offer.open || offer.proposer != seat) return Error::kNoOffer;
  offer = TradeOffer{};
  return Error::kNone;
}

// --- Development cards ---

Error Game::buy_dev_card(int seat) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = verify_turn(seat, Phase::kMain); e != Error::kNone) return e;
  if (dev_deck_next >= dev_deck_size) return Error::kDeckEmpty;
  Player& p = players[at(seat)];
  if (!p.has(kDevCardCost)) return Error::kNotEnoughResources;
  subtract(p.resources, kDevCardCost);
  ++p.new_dev_cards[at(dev_deck[at(dev_deck_next++)])];
  check_victory();  // a victory point card can win on the spot
  return Error::kNone;
}

Error Game::spend_dev_card(int seat, DevCard card) {
  if (game_over) return Error::kGameOver;
  if (seat != current) return Error::kNotYourTurn;
  if (phase != Phase::kRollDice && phase != Phase::kMain) return Error::kWrongPhase;
  if (dev_card_played) return Error::kDevCardAlreadyPlayed;
  Player& p = players[at(seat)];
  if (p.dev_cards[at(card)] == 0) {
    return p.new_dev_cards[at(card)] > 0 ? Error::kDevCardBoughtThisTurn : Error::kNoDevCard;
  }
  --p.dev_cards[at(card)];
  dev_card_played = true;
  return Error::kNone;
}

Error Game::play_knight(int seat) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = spend_dev_card(seat, DevCard::kKnight); e != Error::kNone) return e;
  ++players[at(seat)].knights;
  update_largest_army(seat);
  return_phase = phase;  // before rolling, the turn goes back to rolling
  phase = Phase::kMoveRobber;
  check_victory();
  return Error::kNone;
}

Error Game::play_road_building(int seat) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (!free_road_edges(seat).any()) return Error::kNoRoadSpace;
  if (Error e = spend_dev_card(seat, DevCard::kRoadBuilding); e != Error::kNone) return e;
  free_roads_left = kRoadBuildingRoads;
  return_phase = phase;
  phase = Phase::kRoadBuilding;
  return Error::kNone;
}

Error Game::play_year_of_plenty(int seat, Resource first, Resource second) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  const int wanted_first = first == second ? 2 : 1;
  if (bank_count(first) < wanted_first || bank_count(second) < wanted_first) return Error::kBankEmpty;
  if (Error e = spend_dev_card(seat, DevCard::kYearOfPlenty); e != Error::kNone) return e;
  ++players[at(seat)].resources[at(first)];
  ++players[at(seat)].resources[at(second)];
  return Error::kNone;
}

Error Game::play_monopoly(int seat, Resource resource) {
  if (!valid_seat(seat)) return Error::kBadArgument;
  if (Error e = spend_dev_card(seat, DevCard::kMonopoly); e != Error::kNone) return e;
  int taken = 0;
  for (int s = 0; s < player_count; ++s) {
    if (s == seat) continue;
    taken += players[at(s)].resources[at(resource)];
    players[at(s)].resources[at(resource)] = 0;
  }
  players[at(seat)].resources[at(resource)] += taken;
  return Error::kNone;
}

// --- Legal moves ---

NodeMask Game::legal_settlements(int seat) const {
  if (!valid_seat(seat) || game_over || seat != current) return 0;
  bool free = false;
  if (phase == Phase::kSetup) {
    if (setup_waiting_for_road) return 0;
    free = true;
  } else if (phase == Phase::kMain) {
    if (!players[at(seat)].has(kSettlementCost)) return 0;
  } else {
    return 0;
  }
  const Player& p = players[at(seat)];
  if (count(p.settlements) >= kMaxSettlements) return 0;
  // The same rule as check_settlement_spot, for all 54 corners at once:
  // free corners, minus the neighbors of every building, and outside setup
  // only corners at the end of one of the player's roads.
  const NodeMask taken = occupied();
  NodeMask blocked = taken;
  for_each_node(taken, [&](int n) { blocked |= kTopology.node_neighbor_mask[at(n)]; });
  NodeMask spots = kAllNodes & ~blocked;
  if (!free) {
    NodeMask reached = 0;
    for_each_edge(p.roads, [&](int e) {
      reached |= bit(kTopology.edge_nodes[at(e)][0]) | bit(kTopology.edge_nodes[at(e)][1]);
    });
    spots &= reached;
  }
  return spots;
}

EdgeMask Game::legal_roads(int seat) const {
  if (!valid_seat(seat) || game_over || seat != current) return {};
  if (phase == Phase::kSetup) {
    if (!setup_waiting_for_road) return {};
  } else if (phase == Phase::kRoadBuilding) {
    return free_road_edges(seat);
  } else if (phase != Phase::kMain || !players[at(seat)].has(kRoadCost)) {
    return {};
  }
  return free_road_edges(seat);
}

NodeMask Game::legal_cities(int seat) const {
  if (!valid_seat(seat) || game_over || seat != current || phase != Phase::kMain) return 0;
  const Player& p = players[at(seat)];
  if (!p.has(kCityCost) || count(p.cities) >= kMaxCities) return 0;
  return p.settlements;
}

bool Game::can_buy_dev_card(int seat) const {
  return valid_seat(seat) && !game_over && seat == current && phase == Phase::kMain &&
         dev_deck_next < dev_deck_size && players[at(seat)].has(kDevCardCost);
}

std::uint8_t Game::playable_dev_cards(int seat) const {
  if (!valid_seat(seat) || game_over || seat != current || dev_card_played ||
      (phase != Phase::kRollDice && phase != Phase::kMain)) {
    return 0;
  }
  std::uint8_t out = 0;
  const Player& p = players[at(seat)];
  for (int c = 0; c < kDevCardKinds; ++c) {
    if (c != static_cast<int>(DevCard::kVictoryPoint) && p.dev_cards[at(c)] > 0) out |= seat_bit(c);
  }
  const auto road_building = seat_bit(static_cast<int>(DevCard::kRoadBuilding));
  if ((out & road_building) != 0 && !free_road_edges(seat).any()) {
    out = static_cast<std::uint8_t>(out & ~road_building);
  }
  return out;
}

}  // namespace cae
