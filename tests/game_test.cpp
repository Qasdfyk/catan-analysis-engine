// Rules on hand-made positions. The difference test (tools/difftest.py)
// checks the engine against game.py on thousands of random positions; these
// tests pin down the rare cases random play hardly reaches, and say in one
// place what each rule is.

#include "catan/game.hpp"
#include "catan/record.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace cae {
namespace {

constexpr std::size_t at(int i) { return static_cast<std::size_t>(i); }
constexpr std::size_t at(Resource r) { return static_cast<std::size_t>(r); }

// Hex i is Terrain(i % 5), except the center (9), which is the desert.
Board test_board() {
  static constexpr std::array<int, 18> kNumbers = {5, 2, 6, 3, 8, 10, 9, 12, 11, 4, 8, 10, 9, 4, 5, 6, 3, 11};
  Board b;
  std::size_t next = 0;
  for (int h = 0; h < kHexCount; ++h) {
    if (h == 9) {
      b.terrain[at(h)] = Terrain::kDesert;
      continue;
    }
    b.terrain[at(h)] = static_cast<Terrain>(h % 5);
    b.number[at(h)] = static_cast<std::int8_t>(kNumbers[next++]);
  }
  return b;
}

std::vector<DevCard> deck_starting_with(DevCard first) {
  std::vector<DevCard> deck(kDevDeckSize, DevCard::kKnight);
  deck[0] = first;
  return deck;
}

Game new_game(int players, Settings settings = {}, int first_seat = 0,
              std::vector<DevCard> deck = deck_starting_with(DevCard::kKnight)) {
  return Game::create(test_board(), settings, players, first_seat, deck);
}

// A game past the setup, seat 0 to play its main phase, with no buildings.
Game main_game(int players, Settings settings = {}) {
  Game g = new_game(players, settings);
  g.setup_position = g.setup_length;
  g.current = 0;
  g.phase = Phase::kMain;
  g.dice_roll = 5;
  return g;
}

// Corner d / side d of the hex at (q, r).
int corner(int q, int r, int d) { return node_at(q, r, d); }
int side(int q, int r, int d) { return edge_at(q, r, d); }

int first_node(NodeMask m) { return m == 0 ? -1 : std::countr_zero(m); }
int first_edge(EdgeMask m) {
  int found = -1;
  for_each_edge(m, [&](int e) { if (found < 0) found = e; });
  return found;
}

Cards cards(int wood, int brick, int sheep, int wheat, int ore) { return {wood, brick, sheep, wheat, ore}; }

// The ore hex (1, 0) is hex 14 of the test board.
constexpr int kOreHex = 14;
static_assert(kOreHex % 5 == static_cast<int>(Terrain::kOre));

// Only the ore hex carries a number: 8.
void only_ore_rolls_8(Game& g) {
  g.board.number.fill(0);
  g.board.number[at(kOreHex)] = 8;
  g.board.index_numbers();
}

TEST(Setup, SnakeOrderFromTheFirstSeat) {
  Game g = new_game(4, {}, 1);
  std::vector<int> order;
  for (int i = 0; i < 8; ++i) {
    const int seat = g.current;
    order.push_back(seat);
    ASSERT_EQ(g.build_settlement(seat, first_node(g.legal_settlements(seat))), Error::kNone);
    ASSERT_EQ(g.build_road(seat, first_edge(g.legal_roads(seat))), Error::kNone);
  }
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 0, 0, 3, 2, 1}));
  // The first seat placed last and keeps the turn.
  EXPECT_EQ(g.phase, Phase::kRollDice);
  EXPECT_EQ(g.current, 1);
  EXPECT_EQ(g.turn_number, 8);
}

TEST(Setup, OnlyTheSecondSettlementPaysOut) {
  Game g = new_game(2);  // order 0 1 1 0
  const int far = corner(-2, 0, 3);
  ASSERT_EQ(g.build_settlement(0, far), Error::kNone);
  EXPECT_EQ(g.players[0].card_count(), 0);
  ASSERT_EQ(g.build_road(0, first_edge(g.legal_roads(0))), Error::kNone);
  ASSERT_EQ(g.build_settlement(1, corner(-2, 2, 2)), Error::kNone);
  ASSERT_EQ(g.build_road(1, first_edge(g.legal_roads(1))), Error::kNone);
  // Corner 0 of hex (1,0) touches hexes 14 (ore), 18 (wheat) and 17 (sheep).
  ASSERT_EQ(g.build_settlement(1, corner(1, 0, 0)), Error::kNone);
  EXPECT_EQ(g.players[1].resources, cards(0, 0, 1, 1, 1));
}

TEST(Setup, RoadMustTouchTheSettlementJustPlaced) {
  Game g = new_game(2);
  const int node = corner(-2, 0, 3);
  ASSERT_EQ(g.build_settlement(0, node), Error::kNone);
  EXPECT_EQ(g.build_road(0, side(2, 0, 0)), Error::kMustTouchSetupSettlement);
  EXPECT_EQ(g.build_settlement(0, corner(2, 0, 0)), Error::kWrongPhase);  // a road comes first
  EXPECT_EQ(g.build_road(0, kTopology.node_edges[at(node)][0]), Error::kNone);
}

TEST(Building, DistanceRule) {
  Game g = main_game(2);
  const int node = corner(0, 1, 0);
  g.players[0].settlements = bit(node);
  g.current = 1;
  g.players[1].resources = kSettlementCost;
  const int next_door = kTopology.node_neighbors[at(node)][0];
  EXPECT_EQ(g.build_settlement(1, next_door), Error::kDistanceRule);
  EXPECT_EQ(g.build_settlement(1, node), Error::kOccupied);
}

TEST(Production, SettlementOneCityTwoRobberNone) {
  Game g = main_game(2);
  only_ore_rolls_8(g);
  g.players[0].settlements = bit(corner(1, 0, 0));
  g.players[1].cities = bit(corner(1, 0, 3));
  g.phase = Phase::kRollDice;
  ASSERT_EQ(g.roll_dice(0, 4, 4), Error::kNone);
  EXPECT_EQ(g.players[0].resources[at(Resource::kOre)], 1);
  EXPECT_EQ(g.players[1].resources[at(Resource::kOre)], 2);

  g.robber_hex = kOreHex;
  g.phase = Phase::kRollDice;
  ASSERT_EQ(g.roll_dice(0, 2, 6), Error::kNone);
  EXPECT_EQ(g.players[0].resources[at(Resource::kOre)], 1);
  EXPECT_EQ(g.players[1].resources[at(Resource::kOre)], 2);
}

TEST(Production, ShortBankPaysNobodyWhenSeveralShouldGet) {
  Game g = main_game(3);
  only_ore_rolls_8(g);
  g.players[0].settlements = bit(corner(1, 0, 0));
  g.players[1].cities = bit(corner(1, 0, 3));
  g.players[2].resources[at(Resource::kOre)] = 17;  // the bank has 2 ore, 3 are owed
  g.phase = Phase::kRollDice;
  ASSERT_EQ(g.roll_dice(0, 4, 4), Error::kNone);
  EXPECT_EQ(g.players[0].resources[at(Resource::kOre)], 0);
  EXPECT_EQ(g.players[1].resources[at(Resource::kOre)], 0);
}

TEST(Production, ShortBankGivesASingleReceiverWhatIsLeft) {
  Game g = main_game(2);
  only_ore_rolls_8(g);
  g.players[1].cities = bit(corner(1, 0, 3));
  g.players[0].resources[at(Resource::kOre)] = 18;  // 1 left, 2 owed
  g.phase = Phase::kRollDice;
  ASSERT_EQ(g.roll_dice(0, 4, 4), Error::kNone);
  EXPECT_EQ(g.players[1].resources[at(Resource::kOre)], 1);
  EXPECT_EQ(g.bank_count(Resource::kOre), 0);
}

TEST(Robber, SevenMakesBigHandsDiscardHalfRoundedDown) {
  Game g = main_game(3);
  g.players[0].resources = cards(9, 0, 0, 0, 0);
  g.players[1].resources = cards(0, 8, 0, 0, 0);
  g.players[2].resources = cards(0, 0, 7, 0, 0);  // 7 is the limit: safe
  g.phase = Phase::kRollDice;
  ASSERT_EQ(g.roll_dice(0, 3, 4), Error::kNone);
  EXPECT_EQ(g.phase, Phase::kDiscard);
  EXPECT_EQ(g.pending_discards[0], 4);
  EXPECT_EQ(g.pending_discards[1], 4);
  EXPECT_EQ(g.discard(2, cards(0, 0, 1, 0, 0)), Error::kNothingToDiscard);
  EXPECT_EQ(g.discard(0, cards(3, 0, 0, 0, 0)), Error::kWrongDiscardCount);
  EXPECT_EQ(g.discard(0, cards(4, 0, 0, 0, 0)), Error::kNone);
  EXPECT_EQ(g.phase, Phase::kDiscard);
  EXPECT_EQ(g.discard(1, cards(0, 4, 0, 0, 0)), Error::kNone);
  EXPECT_EQ(g.phase, Phase::kMoveRobber);
}

TEST(Robber, FriendlyRobberSparesPlayersWithTwoPoints) {
  Settings friendly;
  friendly.friendly_robber = true;
  Game g = main_game(2, friendly);
  g.players[1].settlements = bit(corner(1, 0, 0)) | bit(corner(-2, 0, 3));  // 2 points, on the ore hex
  g.phase = Phase::kMoveRobber;
  EXPECT_EQ(g.legal_robber_hexes(0) & (1U << kOreHex), 0U);
  EXPECT_EQ(g.move_robber(0, kOreHex), Error::kFriendlyRobber);
  g.players[1].cities = bit(corner(2, -2, 0));  // now 4 points: fair game
  EXPECT_EQ(g.move_robber(0, kOreHex), Error::kNone);
}

TEST(Awards, LongestRoadNeedsFiveAndAnOpponentCanCutIt) {
  Game g = main_game(2);
  // Seat 0 builds round the ore hex from corner 0: sides 0..4 join corners 0..5.
  g.players[0].settlements = bit(corner(1, 0, 0));
  g.players[0].resources = cards(5, 5, 0, 0, 0);
  for (int d = 0; d < 4; ++d) ASSERT_EQ(g.build_road(0, side(1, 0, d)), Error::kNone);
  EXPECT_EQ(g.longest_road, -1);
  ASSERT_EQ(g.build_road(0, side(1, 0, 4)), Error::kNone);
  EXPECT_EQ(g.longest_road, 0);
  EXPECT_EQ(g.visible_points(0), 3);

  // Seat 1 settles on corner 2, in the middle of that road. Its own road
  // comes in on the third side of that corner, pointing away from the hex.
  const int cut = corner(1, 0, 2);
  for (const auto e : kTopology.node_edges[at(cut)]) {
    if (e >= 0 && !g.players[0].roads.test(e)) g.players[1].roads.set(e);
  }
  g.current = 1;
  g.players[1].resources = kSettlementCost;
  ASSERT_EQ(g.build_settlement(1, cut), Error::kNone);
  EXPECT_EQ(g.road_lengths[0], 3);  // corner 2 to 5; a trail may start at the cut but not pass it
  EXPECT_EQ(g.longest_road, -1);
}

TEST(Awards, LargestArmyGoesToStrictlyMoreKnights) {
  Game g = main_game(2);
  g.players[0].knights = 2;
  g.players[0].dev_cards[0] = 1;
  ASSERT_EQ(g.play_knight(0), Error::kNone);
  EXPECT_EQ(g.largest_army, 0);

  g.current = 1;
  g.phase = Phase::kMain;
  g.dev_card_played = false;
  g.players[1].knights = 2;
  g.players[1].dev_cards[0] = 2;
  ASSERT_EQ(g.play_knight(1), Error::kNone);  // 3 against 3: the holder keeps it
  EXPECT_EQ(g.largest_army, 0);
  g.phase = Phase::kMain;
  g.dev_card_played = false;
  ASSERT_EQ(g.play_knight(1), Error::kNone);  // 4 against 3
  EXPECT_EQ(g.largest_army, 1);
}

TEST(DevCards, NotInTheTurnBoughtAndOnePerTurn) {
  Game g = main_game(2);
  g.players[0].resources = cards(0, 0, 2, 2, 2);
  ASSERT_EQ(g.buy_dev_card(0), Error::kNone);
  EXPECT_EQ(g.play_knight(0), Error::kDevCardBoughtThisTurn);
  ASSERT_EQ(g.end_turn(0), Error::kNone);
  ASSERT_EQ(g.roll_dice(1, 1, 2), Error::kNone);
  ASSERT_EQ(g.end_turn(1), Error::kNone);
  // Back to seat 0, before rolling: a knight may be played now.
  g.players[0].dev_cards[0] = 2;
  ASSERT_EQ(g.play_knight(0), Error::kNone);
  EXPECT_EQ(g.return_phase, Phase::kRollDice);
  ASSERT_EQ(g.move_robber(0, kOreHex), Error::kNone);
  EXPECT_EQ(g.phase, Phase::kRollDice);
  EXPECT_EQ(g.play_knight(0), Error::kDevCardAlreadyPlayed);
}

TEST(DevCards, VictoryPointCardWinsTheMomentItIsBought) {
  Game g = new_game(2, {}, 0, deck_starting_with(DevCard::kVictoryPoint));
  g.setup_position = g.setup_length;
  g.phase = Phase::kMain;
  g.players[0].settlements = bit(corner(-2, 0, 3)) | bit(corner(0, 1, 0)) | bit(corner(2, -2, 0)) |
                             bit(corner(-1, -1, 5)) | bit(corner(1, 1, 2));
  g.players[0].cities = bit(corner(-2, 2, 2)) | bit(corner(2, 0, 0));  // 5 + 4 = 9 points
  g.players[0].resources = kDevCardCost;
  ASSERT_EQ(g.buy_dev_card(0), Error::kNone);
  EXPECT_TRUE(g.game_over);
  EXPECT_EQ(g.winner, 0);
  EXPECT_EQ(g.end_turn(0), Error::kGameOver);
}

TEST(DevCards, YearOfPlentyNeedsTheBankToHaveTheCards) {
  Game g = main_game(2);
  g.players[0].dev_cards[at(static_cast<int>(DevCard::kYearOfPlenty))] = 1;
  g.players[1].resources[at(Resource::kWood)] = 18;  // one wood left
  EXPECT_EQ(g.play_year_of_plenty(0, Resource::kWood, Resource::kWood), Error::kBankEmpty);
  EXPECT_EQ(g.play_year_of_plenty(0, Resource::kWood, Resource::kBrick), Error::kNone);
  EXPECT_EQ(g.players[0].resources, cards(1, 1, 0, 0, 0));
}

TEST(Trading, PortsSetTheBankRate) {
  Game g = main_game(2);
  const int a = corner(-2, 0, 3);
  const int b = corner(-2, 0, 4);
  g.board.ports[0] = {PortType::kWood, bit(a) | bit(b)};
  g.board.ports[1] = {PortType::kAny, bit(corner(2, -2, 0))};
  g.board.port_count = 2;
  EXPECT_EQ(g.trade_rate(0, Resource::kWood), 4);
  g.players[0].settlements = bit(a);
  EXPECT_EQ(g.trade_rate(0, Resource::kWood), 2);
  EXPECT_EQ(g.trade_rate(0, Resource::kBrick), 4);
  g.players[0].cities = bit(corner(2, -2, 0));  // a city uses a port too
  EXPECT_EQ(g.trade_rate(0, Resource::kBrick), 3);
  EXPECT_EQ(g.trade_rate(0, Resource::kWood), 2);
}

TEST(Trading, OfferAcceptConfirmAndStaleOffers) {
  Game g = main_game(3);
  g.players[0].resources = cards(2, 0, 0, 0, 0);
  g.players[1].resources = cards(0, 0, 0, 0, 1);
  g.players[2].resources = cards(0, 0, 0, 0, 1);
  ASSERT_EQ(g.offer_trade(0, cards(1, 0, 0, 0, 0), cards(0, 0, 0, 0, 1)), Error::kNone);
  EXPECT_EQ(g.respond_trade(0, true), Error::kBadTrade);  // not one's own offer
  ASSERT_EQ(g.respond_trade(1, true), Error::kNone);
  ASSERT_EQ(g.respond_trade(2, true), Error::kNone);
  EXPECT_EQ(g.confirm_trade(0, 2), Error::kNone);
  EXPECT_EQ(g.players[0].resources, cards(1, 0, 0, 0, 1));
  EXPECT_EQ(g.players[2].resources, cards(1, 0, 0, 0, 0));
  EXPECT_FALSE(g.offer.open);

  // An offer accepted earlier fails if the partner no longer has the cards.
  ASSERT_EQ(g.offer_trade(0, cards(1, 0, 0, 0, 0), cards(0, 0, 0, 0, 1)), Error::kNone);
  ASSERT_EQ(g.respond_trade(1, true), Error::kNone);
  g.players[1].resources = {};
  EXPECT_EQ(g.confirm_trade(0, 1), Error::kNotEnoughResources);
  EXPECT_EQ(g.offer_trade(0, cards(1, 0, 0, 0, 0), cards(1, 0, 0, 0, 0)), Error::kBadTrade);
}

TEST(Refusals, LeaveTheStateExactlyAsItWas) {
  Game g = main_game(3);
  g.players[0].settlements = bit(corner(1, 0, 0));
  g.players[0].resources = cards(1, 0, 1, 0, 0);
  const auto before = snapshot(g).dump();
  EXPECT_NE(g.build_road(0, side(-2, 0, 0)), Error::kNone);          // not connected
  EXPECT_NE(g.build_settlement(0, corner(1, 0, 1)), Error::kNone);   // next door
  EXPECT_NE(g.upgrade_city(0, corner(1, 0, 0)), Error::kNone);       // no ore
  EXPECT_NE(g.roll_dice(0, 3, 3), Error::kNone);                     // already rolled
  EXPECT_NE(g.end_turn(1), Error::kNone);                            // not their turn
  EXPECT_NE(g.trade_with_bank(0, Resource::kWood, Resource::kOre), Error::kNone);
  EXPECT_NE(g.steal(0, 1, Resource::kWood), Error::kNone);
  EXPECT_EQ(snapshot(g).dump(), before);
}

}  // namespace
}  // namespace cae
