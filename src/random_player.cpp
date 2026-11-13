#include "catan/random_player.hpp"

#include <algorithm>
#include <bit>
#include <tuple>

namespace cae {

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

constexpr std::array<Resource, kResourceCount> kResources = {
    Resource::kWood, Resource::kBrick, Resource::kSheep, Resource::kWheat, Resource::kOre};

// Fisher-Yates, the same algorithm as Python's random.shuffle.
template <typename T, std::size_t N>
void shuffle(std::array<T, N>& items, Rng& rng) {
  for (int i = static_cast<int>(N) - 1; i > 0; --i) {
    std::swap(items[at(i)], items[at(rng.below(i + 1))]);
  }
}

// The k-th set bit (k = 0 is the lowest) of a 64-bit word.
int nth_bit(std::uint64_t bits, int k) {
  for (; k > 0; --k) bits &= bits - 1;
  return std::countr_zero(bits);
}

// A uniformly chosen member of a non-empty set, the rng.choice() of a list.
int pick(std::uint64_t bits, Rng& rng) { return nth_bit(bits, rng.below(std::popcount(bits))); }

int pick(const EdgeMask& edges, Rng& rng) {
  const int k = rng.below(edges.count());
  const int low = std::popcount(edges.lo);
  return k < low ? nth_bit(edges.lo, k) : 64 + nth_bit(edges.hi, k - low);
}

// One card drawn from a hand, each card equally likely (not each resource):
// how the platform picks the card a thief takes.
Resource random_card(const Cards& hand, Rng& rng) {
  int k = rng.below(std::max(1, hand[0] + hand[1] + hand[2] + hand[3] + hand[4]));
  for (std::size_t r = 0; r < kResourceCount; ++r) {
    if (k < hand[r]) return static_cast<Resource>(r);
    k -= hand[r];
  }
  return Resource::kWood;  // unreachable for a non-empty hand
}

bool red_numbers_touch(const Board& board) {
  for (int h = 0; h < kHexCount; ++h) {
    const int n = board.number[at(h)];
    if (n != 6 && n != 8) continue;
    for (int other = 0; other < kHexCount; ++other) {
      const int m = board.number[at(other)];
      if (other == h || (m != 6 && m != 8)) continue;
      // Two hexes are neighbors when they share a side, i.e. two corners.
      if (count(kTopology.hex_node_mask[at(h)] & kTopology.hex_node_mask[at(other)]) == 2) return true;
    }
  }
  return false;
}

// The 30 sides that touch exactly one land hex, as a closed walk along the
// coast, starting where Board._coastal_edges_in_order starts.
std::array<int, 30> coast_in_order() {
  auto land_hexes = [](int e) {
    const NodeMask ends = bit(kTopology.edge_nodes[at(e)][0]) | bit(kTopology.edge_nodes[at(e)][1]);
    int n = 0;
    for (int h = 0; h < kHexCount; ++h) n += (kTopology.hex_node_mask[at(h)] & ends) == ends ? 1 : 0;
    return n;
  };
  EdgeMask remaining;
  int current = -1;
  for (int e = 0; e < kEdgeCount; ++e) {
    if (land_hexes(e) != 1) continue;
    remaining.set(e);
    const Spelling& s = kTopology.edge_spelling[at(e)];
    const Spelling& best = kTopology.edge_spelling[at(current < 0 ? e : current)];
    if (current < 0 || std::tie(s.hex.q, s.hex.r, s.direction) < std::tie(best.hex.q, best.hex.r, best.direction)) {
      current = e;
    }
  }
  std::array<int, 30> walk{};
  int length = 0;
  remaining.reset(current);
  walk[at(length++)] = current;
  int node = kTopology.edge_nodes[at(current)][1];
  while (remaining.any() && length < 30) {
    for (const std::int8_t e : kTopology.node_edges[at(node)]) {
      if (e < 0 || !remaining.test(e)) continue;
      current = e;
      break;
    }
    remaining.reset(current);
    walk[at(length++)] = current;
    const auto& ends = kTopology.edge_nodes[at(current)];
    node = ends[0] == node ? ends[1] : ends[0];
  }
  return walk;
}

// --- The policy, one function per branch of random_player.py ---

// Keeps the first refusal of a step. The policy only picks legal moves, so a
// refusal is a bug in the player or the engine; the tests check for none.
void note(Error& err, Error e) {
  if (err == Error::kNone) err = e;
}


bool play_dev_card(Game& g, int seat, Rng& rng, Error& err) {
  const std::uint8_t playable = g.playable_dev_cards(seat);
  if (playable == 0 || rng.chance(0.5)) return false;
  const auto card = static_cast<DevCard>(pick(playable, rng));
  switch (card) {
    case DevCard::kKnight:
      note(err, g.play_knight(seat));
      break;
    case DevCard::kRoadBuilding:
      note(err, g.play_road_building(seat));
      break;
    case DevCard::kMonopoly:
      note(err, g.play_monopoly(seat, kResources[at(rng.below(kResourceCount))]));
      break;
    default: {  // year of plenty: twice the same resource, one the bank has 2 of
      std::uint64_t available = 0;
      for (int r = 0; r < kResourceCount; ++r) {
        if (g.bank_count(static_cast<Resource>(r)) >= 2) available |= std::uint64_t{1} << r;
      }
      if (available == 0) return false;
      const auto first = static_cast<Resource>(pick(available, rng));
      note(err, g.play_year_of_plenty(seat, first, first));
    }
  }
  return true;
}

bool try_player_trade(Game& g, int seat, Rng& rng, Error& err) {
  const Cards& hand = g.players[at(seat)].resources;
  std::uint64_t mine = 0;
  for (int r = 0; r < kResourceCount; ++r) mine |= hand[at(r)] > 0 ? std::uint64_t{1} << r : 0;
  // A random opponent: one of the other player_count - 1 seats.
  int partner = rng.below(g.player_count - 1);
  if (partner >= seat) ++partner;
  const Cards& other = g.players[at(partner)].resources;
  std::uint64_t theirs = 0;
  for (int r = 0; r < kResourceCount; ++r) theirs |= other[at(r)] > 0 ? std::uint64_t{1} << r : 0;
  theirs &= ~mine;
  if (mine == 0 || theirs == 0) return false;
  Cards give{};
  Cards get{};
  give[at(pick(mine, rng))] = 1;
  get[at(pick(theirs, rng))] = 1;
  note(err, g.offer_trade(seat, give, get));
  note(err, g.respond_trade(partner, true));
  note(err, g.confirm_trade(seat, partner));
  return true;
}

void discard_randomly(Game& g, Rng& rng, Error& err) {
  for (int s = 0; s < g.player_count; ++s) {
    const int required = g.pending_discards[at(s)];
    if (required <= 0) continue;
    // random.sample of `required` cards: draw them one by one without putting back.
    Cards hand = g.players[at(s)].resources;
    Cards cards{};
    for (int i = 0; i < required; ++i) {
      const auto r = at(static_cast<int>(random_card(hand, rng)));
      --hand[r];
      ++cards[r];
    }
    note(err, g.discard(s, cards));
    return;
  }
}

void main_phase(Game& g, int seat, Rng& rng, Error& err) {
  if (rng.chance(0.1) && try_player_trade(g, seat, rng, err)) return;
  if (g.can_buy_dev_card(seat) && rng.chance(0.5)) {
    note(err, g.buy_dev_card(seat));
  } else if (const NodeMask cities = g.legal_cities(seat); cities != 0) {
    note(err, g.upgrade_city(seat, pick(cities, rng)));
  } else if (const NodeMask spots = g.legal_settlements(seat); spots != 0) {
    note(err, g.build_settlement(seat, pick(spots, rng)));
  } else if (const EdgeMask roads = g.legal_roads(seat); roads.any() && rng.chance(0.7)) {
    note(err, g.build_road(seat, pick(roads, rng)));
  } else {
    const Cards& hand = g.players[at(seat)].resources;
    for (const Resource give : kResources) {
      // The bank can run out of a resource, so only those it still has are asked for.
      std::uint64_t wanted = 0;
      for (const Resource r : kResources) {
        if (r != give && g.bank_count(r) > 0) wanted |= std::uint64_t{1} << static_cast<int>(r);
      }
      if (wanted != 0 && hand[at(static_cast<int>(give))] >= g.trade_rate(seat, give)) {
        note(err, g.trade_with_bank(seat, give, static_cast<Resource>(pick(wanted, rng))));
        return;
      }
    }
    note(err, g.end_turn(seat));
  }
}

}  // namespace

Board random_board(Rng& rng) {
  Board board;
  std::array<Terrain, kHexCount> terrain{};
  std::fill_n(terrain.begin(), 4, Terrain::kWood);
  std::fill_n(terrain.begin() + 4, 4, Terrain::kSheep);
  std::fill_n(terrain.begin() + 8, 4, Terrain::kWheat);
  std::fill_n(terrain.begin() + 12, 3, Terrain::kBrick);
  std::fill_n(terrain.begin() + 15, 3, Terrain::kOre);
  terrain[18] = Terrain::kDesert;
  std::array<std::int8_t, 18> numbers = {2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12};
  do {
    shuffle(terrain, rng);
    shuffle(numbers, rng);
    board.terrain = terrain;
    std::size_t next = 0;
    for (std::size_t h = 0; h < kHexCount; ++h) {
      board.number[h] = terrain[h] == Terrain::kDesert ? std::int8_t{0} : numbers[next++];
    }
  } while (red_numbers_touch(board));

  std::array<PortType, kMaxPorts> ports = {PortType::kAny,   PortType::kAny,  PortType::kAny,
                                           PortType::kAny,   PortType::kWood, PortType::kBrick,
                                           PortType::kSheep, PortType::kWheat, PortType::kOre};
  shuffle(ports, rng);
  static const std::array<int, 30> coast = coast_in_order();
  int index = 0;
  for (int i = 0; i < kMaxPorts; ++i) {
    const auto& ends = kTopology.edge_nodes[at(coast[at(index)])];
    board.ports[at(i)] = Port{ports[at(i)], bit(ends[0]) | bit(ends[1])};
    index += i % 3 == 2 ? 4 : 3;
  }
  board.port_count = kMaxPorts;
  board.index_numbers();
  return board;
}

std::array<DevCard, kDevDeckSize> random_dev_deck(Rng& rng) {
  std::array<DevCard, kDevDeckSize> deck{};
  std::fill_n(deck.begin(), 14, DevCard::kKnight);
  std::fill_n(deck.begin() + 14, 5, DevCard::kVictoryPoint);
  std::fill_n(deck.begin() + 19, 2, DevCard::kRoadBuilding);
  std::fill_n(deck.begin() + 21, 2, DevCard::kYearOfPlenty);
  std::fill_n(deck.begin() + 23, 2, DevCard::kMonopoly);
  shuffle(deck, rng);
  return deck;
}

Game random_game(Rng& rng, int players) {
  const Board board = random_board(rng);
  const auto deck = random_dev_deck(rng);
  return Game::create(board, Settings{}, players, rng.below(players), deck);
}

Error play_random_step(Game& g, Rng& rng) {
  Error err = Error::kNone;
  if (g.game_over) return Error::kGameOver;
  if (g.phase == Phase::kDiscard) {
    discard_randomly(g, rng, err);
    return err;
  }
  const int seat = g.current;
  if ((g.phase == Phase::kRollDice || g.phase == Phase::kMain) && play_dev_card(g, seat, rng, err)) return err;

  switch (g.phase) {
    case Phase::kRollDice:
      note(err, g.roll_dice(seat, rng.die(), rng.die()));
      break;
    case Phase::kMoveRobber:
      note(err, g.move_robber(seat, pick(g.legal_robber_hexes(seat), rng)));
      break;
    case Phase::kSteal: {
      const int victim = pick(g.steal_victims(seat), rng);
      note(err, g.steal(seat, victim, random_card(g.players[at(victim)].resources, rng)));
      break;
    }
    case Phase::kRoadBuilding:
      note(err, g.build_road(seat, pick(g.legal_roads(seat), rng)));
      break;
    case Phase::kSetup:
      if (g.setup_waiting_for_road) {
        note(err, g.build_road(seat, pick(g.legal_roads(seat), rng)));
      } else {
        note(err, g.build_settlement(seat, pick(g.legal_settlements(seat), rng)));
      }
      break;
    case Phase::kMain:
      main_phase(g, seat, rng, err);
      break;
    case Phase::kDiscard:
      break;  // handled above
  }
  return err;
}

PlayoutResult play_out(Game& game, Rng& rng, int max_actions) {
  PlayoutResult result;
  while (!game.game_over && result.actions < max_actions) {
    if (play_random_step(game, rng) != Error::kNone) ++result.refused;
    ++result.actions;
  }
  result.winner = game.winner;
  return result;
}

}  // namespace cae
