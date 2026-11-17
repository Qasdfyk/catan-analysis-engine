// catan_zoo_check: does the C++ zoo play exactly like the Go one?
//
// Replays game records played by catan-project's hard and zoo bots. Before
// every move such a bot made, it asks the C++ port (zoo.hpp) what it would
// do in the same position, as the same bot (the record keeps each zoo bot's
// shaken weights), and compares the two moves. Chance is not a decision, so
// only the choice is compared: for a roll, that it rolled; for a steal, whom
// it robbed.
//
// The platform plays in real time, so a recorded move is not always a
// decision on the position just before it. A bot thinks on the newest state
// it has received, and the state may move on before its move arrives (an
// offer withdrawn while it answered, another answer coming in). After the
// server refuses a move, the Go bot falls back to a safe one: it withdraws
// its own offer or ends its turn. The tournament log of 400 zoo games has
// over 30,000 such refusals. So every recorded move falls in one of:
//   same      the C++ bot makes this move in the position before it
//   earlier   it makes it in an earlier position of this turn or the one
//             before, or since the offer before the open one. A bot may
//             think on a state that is a few moves old (an answer to an
//             older offer can land on the open one),
//             and it may send one move two or three times when newer states
//             that still ask for it arrive before the server applied the
//             first.
//   fallback  a safe move after a refusal: cancel_trade or end_turn in the
//             main phase, or rolling instead of playing a knight
//   differ    none of these: a difference between the two bots
//
// Usage: catan_zoo_check [--show N] RECORD.json...
// Exits 1 if any move differs or a record does not replay.

#include "catan/record.hpp"
#include "catan/zoo.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

using cae::Action;
using cae::ActionKind;
using json = nlohmann::json;

constexpr int kKinds = static_cast<int>(ActionKind::kEndTurn) + 1;

std::string describe(const Action& a) {
  std::string s(cae::action_name(a.kind));
  if (a.target >= 0) s += " target=" + std::to_string(a.target);
  const auto cards = [](const cae::Cards& c) {
    std::string out = "[";
    for (std::size_t i = 0; i < c.size(); ++i) out += (i ? "," : "") + std::to_string(c[i]);
    return out + "]";
  };
  switch (a.kind) {
    case ActionKind::kDiscard: s += " cards=" + cards(a.give); break;
    case ActionKind::kOfferTrade: s += " give=" + cards(a.give) + " get=" + cards(a.get); break;
    case ActionKind::kRespondTrade: s += a.accept ? " yes" : " no"; break;
    case ActionKind::kTradeWithBank:
    case ActionKind::kPlayYearOfPlenty:
      s += " " + std::string(cae::resource_name(a.first)) + "," + std::string(cae::resource_name(a.second));
      break;
    case ActionKind::kPlayMonopoly: s += " " + std::string(cae::resource_name(a.first)); break;
    default: break;
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  int show = 20;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--show" && i + 1 < argc) {
      show = std::atoi(argv[++i]);
    } else {
      files.emplace_back(arg);
    }
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: catan_zoo_check [--show N] RECORD.json...\n");
    return 2;
  }

  enum Verdict { kSame, kEarlier, kFallback, kDiffer, kVerdicts };
  std::array<std::array<long, kVerdicts>, kKinds> tally{};
  long broken = 0;
  long games = 0;
  int shown = 0;

  for (const std::string& path : files) {
    json record;
    cae::Game game;
    std::array<std::optional<cae::ZooBot>, cae::kMaxPlayers> bots{};
    try {
      record = cae::read_record_file(path);
      game = cae::game_from_record(record);
      for (const json& player : record["players"]) {
        const int seat = player.value("seat", -1);
        if (seat >= 0 && seat < cae::kMaxPlayers) bots[static_cast<std::size_t>(seat)] = cae::zoo_bot_from_record(player);
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
      ++broken;
      continue;
    }
    ++games;
    cae::TableMemory memory;
    const json& events = record["events"];
    // Every state so far with the memory that went with it, and where the
    // previous turn began.
    std::vector<std::pair<cae::Game, cae::TableMemory>> states;
    states.reserve(events.size() + 1);
    std::size_t turn_start = 0;
    std::size_t previous_turn_start = 0;
    std::size_t last_offer = 0;
    std::size_t previous_offer = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
      const json& event = events[i];
      const int seat = event.value("seat", -1);
      const auto played = cae::action_from_event(event);
      if (!played || seat < 0 || seat >= game.player_count) {
        std::fprintf(stderr, "%s: event %zu does not parse\n", path.c_str(), i);
        ++broken;
        break;
      }
      memory.sync(game);
      if (states.empty() || states.back().first.turn_number != game.turn_number) {
        previous_turn_start = turn_start;
        turn_start = states.size();
      }
      if (played && played->kind == ActionKind::kOfferTrade) {
        previous_offer = last_offer;
        last_offer = states.size();
      }
      states.emplace_back(game, memory);
      if (const auto& bot = bots[static_cast<std::size_t>(seat)]) {
        const auto kind = static_cast<std::size_t>(played->kind);
        const auto chosen = cae::zoo_decide(game, seat, *bot, memory);
        Verdict verdict = kDiffer;
        if (chosen && *chosen == *played) {
          verdict = kSame;
        } else {
          for (std::size_t k = std::min(previous_turn_start, previous_offer); k + 1 < states.size(); ++k) {
            const auto then = cae::zoo_decide(states[k].first, seat, *bot, states[k].second);
            if (then && *then == *played) {
              verdict = kEarlier;
              break;
            }
          }
          const bool main = game.phase == cae::Phase::kMain && game.current == seat;
          if (verdict == kDiffer &&
              ((main && (played->kind == ActionKind::kCancelTrade || played->kind == ActionKind::kEndTurn)) ||
               (played->kind == ActionKind::kRollDice && chosen && chosen->kind == ActionKind::kPlayKnight))) {
            verdict = kFallback;
          }
        }
        ++tally[kind][verdict];
        if (verdict == kDiffer) {
          if (shown++ < show) {
            std::printf("%s event %zu seat %d (%s):\n  Go  played %s\n  C++ chose  %s\n", path.c_str(), i, seat,
                        std::string(cae::phase_name(game.phase)).c_str(), describe(*played).c_str(),
                        chosen ? describe(*chosen).c_str() : "nothing");
          }
        }
      }
      if (const cae::Error e = cae::apply_event(game, event); e != cae::Error::kNone) {
        std::fprintf(stderr, "%s: event %zu refused: %s\n", path.c_str(), i,
                     std::string(cae::error_name(e)).c_str());
        ++broken;
        break;
      }
      memory.record(seat, *played);
    }
  }

  std::array<long, kVerdicts> all{};
  std::printf("\n%-22s %9s %9s %9s %9s\n", "move", "same", "earlier", "fallback", "differ");
  for (int k = 0; k < kKinds; ++k) {
    const auto& row = tally[static_cast<std::size_t>(k)];
    if (row[kSame] + row[kEarlier] + row[kFallback] + row[kDiffer] == 0) continue;
    std::printf("%-22s %9ld %9ld %9ld %9ld\n", std::string(cae::action_name(static_cast<ActionKind>(k))).c_str(),
                row[kSame], row[kEarlier], row[kFallback], row[kDiffer]);
    for (int v = 0; v < kVerdicts; ++v) all[static_cast<std::size_t>(v)] += row[static_cast<std::size_t>(v)];
  }
  std::printf("%-22s %9ld %9ld %9ld %9ld\n", "all", all[kSame], all[kEarlier], all[kFallback], all[kDiffer]);
  const long wrong = all[kDiffer];
  std::printf("%ld games, %ld broken records\n", games, broken);
  return wrong == 0 && broken == 0 ? 0 : 1;
}
