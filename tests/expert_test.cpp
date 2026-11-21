// The expert level: moves go to the platform's form and back unchanged, a
// record rebuilds the table the arena keeps, and the move is legal,
// reproducible and on time.

#include "catan/expert.hpp"
#include "catan/random_player.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cae {
namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

json read_record(const std::string& name) {
  std::ifstream in(std::string(CATAN_TEST_DATA_DIR) + "/" + name);
  std::stringstream text;
  text << in.rdbuf();
  return json::parse(text.str());
}

json cut(json record, int events) {
  record["events"] = json(std::vector<json>(record["events"].begin(), record["events"].begin() + events));
  return record;
}

// The fields a kind uses, so actions built in different ways compare.
json normal(const Action& a) { return action_json(a); }

Action make(ActionKind kind, int target = -1) {
  Action a;
  a.kind = kind;
  a.target = target;
  return a;
}

// Every move of every kind, legal somewhere or not: every corner, side, hex
// and seat, every pair of resources and some sets of cards.
std::vector<Action> every_move() {
  std::vector<Action> out;
  for (int n = 0; n < kNodeCount; ++n) {
    out.push_back(make(ActionKind::kBuildSettlement, n));
    out.push_back(make(ActionKind::kUpgradeCity, n));
  }
  for (int e = 0; e < kEdgeCount; ++e) out.push_back(make(ActionKind::kBuildRoad, e));
  for (int h = 0; h < kHexCount; ++h) out.push_back(make(ActionKind::kMoveRobber, h));
  for (int s = 0; s < kMaxPlayers; ++s) {
    out.push_back(make(ActionKind::kSteal, s));
    out.push_back(make(ActionKind::kConfirmTrade, s));
  }
  for (int a = 0; a < kResourceCount; ++a) {
    Action m = make(ActionKind::kPlayMonopoly);
    m.first = static_cast<Resource>(a);
    out.push_back(m);
    for (int b = 0; b < kResourceCount; ++b) {
      for (const ActionKind k : {ActionKind::kTradeWithBank, ActionKind::kPlayYearOfPlenty}) {
        Action t = make(k);
        t.first = static_cast<Resource>(a);
        t.second = static_cast<Resource>(b);
        out.push_back(t);
      }
      Action offer = make(ActionKind::kOfferTrade);
      offer.give[at(a)] = 1 + b;
      offer.get[at(b)] += 1;
      out.push_back(offer);
      Action discard = make(ActionKind::kDiscard);
      discard.give[at(a)] = 2;
      discard.give[at(b)] += 1;
      out.push_back(discard);
    }
  }
  for (const bool yes : {true, false}) {
    Action r = make(ActionKind::kRespondTrade);
    r.accept = yes;
    out.push_back(r);
  }
  for (const ActionKind k : {ActionKind::kRollDice, ActionKind::kCancelTrade, ActionKind::kBuyDevCard,
                             ActionKind::kPlayKnight, ActionKind::kPlayRoadBuilding, ActionKind::kEndTurn}) {
    out.push_back(make(k));
  }
  return out;
}

TEST(Expert, EveryMoveGoesToThePlatformFormAndBack) {
  std::vector<Action> moves = every_move();
  // And the moves bots really make, offers and discards included.
  for (std::uint64_t seed = 1; seed <= 4; ++seed) {
    Rng rng(seed);
    ZooTable t;
    t.game = Game::create(random_board(rng), Settings{}, 4, rng.below(4), random_dev_deck(rng));
    for (auto& b : t.bots) b = hard_bot();
    for (int step = 0; step < 4000 && !t.game.game_over; ++step) {
      t.memory.sync(t.game);
      const int s = next_actor(t.game);
      if (s < 0) break;
      if (const auto a = zoo_decide(t.game, s, t.bots[at(s)], t.memory)) moves.push_back(*a);
      (void)play_zoo_step(t, rng);
    }
  }
  for (const Action& a : moves) {
    const json event = event_from_action(a);
    const auto back = action_from_event(event);
    ASSERT_TRUE(back.has_value()) << event.dump();
    ASSERT_EQ(normal(*back), normal(a)) << event.dump();
  }
  EXPECT_GT(moves.size(), 1000U);
}

TEST(Expert, RebuildsWhatTheSeatKnowsFromTheRecord) {
  for (const char* name : {"zoo-game-00088.json", "hard-game-00030.json"}) {
    const json record = read_record(name);
    const ReplayLog log = ReplayLog::from_text(record.dump());
    ASSERT_TRUE(log.error().empty()) << log.error();
    const std::vector<RecordDecision> decisions = spread_decisions(log.states(), 12);
    ASSERT_FALSE(decisions.empty());
    for (const RecordDecision& d : decisions) {
      const TablePosition p = table_from_record(cut(record, d.state));
      EXPECT_EQ(p.events, d.state);
      EXPECT_EQ(snapshot(p.game), snapshot(log.state(d.state))) << name << " state " << d.state;
      EXPECT_EQ(p.belief.known[at(d.seat)], d.known) << name << " state " << d.state;
      // record_decisions() sees only the states, so its memory has the
      // settlements but not the offers of the turn (table_from_record reads
      // them from the events); only the settlements compare.
      EXPECT_EQ(p.memory.built_count, d.memory.built_count);
    }
  }
}

TEST(Expert, MovesAreLegalAndReproducibleWithoutATimeLimit) {
  const json record = read_record("zoo-game-00088.json");
  const ReplayLog log = ReplayLog::from_text(record.dump());
  ExpertSettings settings;
  settings.search.iterations = 64;
  settings.threads = 2;
  settings.time_limit = std::chrono::milliseconds(0);
  int searched = 0;
  for (const RecordDecision& d : spread_decisions(log.states(), 6)) {
    const TablePosition p = table_from_record(cut(record, d.state));
    const ExpertMove first = expert_move(p, d.seat, {}, settings, 3);
    const ExpertMove again = expert_move(p, d.seat, {}, settings, 3);
    ASSERT_TRUE(first.action.has_value());
    ASSERT_TRUE(again.action.has_value());
    EXPECT_EQ(normal(*first.action), normal(*again.action));
    // Legal: the rules take it as the platform would send it.
    Game g = p.game;
    json event = event_from_action(*first.action);
    event["seat"] = d.seat;
    if (first.action->kind == ActionKind::kRollDice) event["outcome"] = {{"dice", {3, 4}}};
    EXPECT_EQ(apply_event(g, event), Error::kNone) << event.dump();
    if (first.searched) {
      ++searched;
      EXPECT_EQ(first.search.iterations, 64);
    }
  }
  EXPECT_GT(searched, 0);
}

TEST(Expert, HasNoMoveForASeatThatNeedNotAct) {
  const json record = read_record("zoo-game-00088.json");
  const TablePosition p = table_from_record(cut(record, 60));
  const int mover = next_actor(p.game);
  ASSERT_GE(mover, 0);
  ExpertSettings settings;
  settings.time_limit = std::chrono::milliseconds(0);
  settings.search.iterations = 16;
  for (int s = 0; s < p.game.player_count; ++s) {
    const ExpertMove m = expert_move(p, s, {}, settings, 1);
    if (s == mover) {
      EXPECT_TRUE(m.action.has_value());
    } else if (!p.game.offer.open && p.game.phase != Phase::kDiscard) {
      EXPECT_FALSE(m.action.has_value()) << "seat " << s;
    }
  }
}

TEST(Expert, ADeadlineInThePastStopsAfterOnePlayout) {
  const json record = read_record("zoo-game-00088.json");
  const ReplayLog log = ReplayLog::from_text(record.dump());
  const RecordDecision d = spread_decisions(log.states(), 4).at(2);
  const TablePosition p = table_from_record(cut(record, d.state));
  MctsSettings s;
  s.iterations = 400;
  s.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  const MctsSearch search = mcts_search(p.game, d.seat, d.known, d.memory, hard_bot(), {}, s, 1);
  EXPECT_EQ(search.iterations, 1);
}

TEST(Expert, RefusesARecordWithAnImpossibleEvent) {
  json record = cut(read_record("zoo-game-00088.json"), 20);
  record["events"].push_back({{"turn", 0}, {"seat", 0}, {"action", "upgrade_city"}, {"payload", {{"hex", {{"q", 0}, {"r", 0}, {"s", 0}}}, {"direction", 0}}}});
  EXPECT_THROW((void)table_from_record(record), RecordError);
}

}  // namespace
}  // namespace cae
