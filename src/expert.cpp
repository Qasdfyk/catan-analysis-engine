#include "catan/expert.hpp"

#include "catan/record.hpp"

#include <algorithm>
#include <string>
#include <thread>
#include <utility>

namespace cae {

namespace {

using json = nlohmann::json;

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// The same mixing as mcts.cpp and arena.cpp: one stream per (seed, index).
constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept {
  return a * 0x9e3779b97f4a7c15ULL + b + 0x632be59bd9b4e019ULL;
}

}  // namespace

void apply_record_event(TablePosition& p, const json& event) {
  // The same bookkeeping as play_arena_game (arena.cpp), in the same order.
  p.memory.sync(p.game);
  const Game before = p.game;
  const Error e = apply_event(p.game, event);
  if (e != Error::kNone) {
    throw RecordError("event " + std::to_string(p.events) + " (" + event.value("action", "?") +
                      ") is refused by the rules");
  }
  const std::optional<Action> action = action_from_event(event);
  const int seat = event["seat"].get<int>();
  if (action && action->kind != ActionKind::kBuildSettlement) p.memory.record(seat, *action);
  for (int s = 0; s < p.game.player_count; ++s) {
    const Player& was = before.players[at(s)];
    const Player& now = p.game.players[at(s)];
    for_each_node(now.settlements & ~was.settlements, [&](int n) {
      p.built.push_back({Built::Kind::kSettlement, s, n});
      Action placed;
      placed.kind = ActionKind::kBuildSettlement;
      placed.target = n;
      p.memory.record(s, placed);
    });
    for_each_node(now.cities & ~was.cities, [&](int n) { p.built.push_back({Built::Kind::kCity, s, n}); });
    for_each_edge(now.roads & ~was.roads, [&](int n) { p.built.push_back({Built::Kind::kRoad, s, n}); });
  }
  p.belief.observe(before, p.game);
  ++p.events;
  p.memory.sync(p.game);
}

TablePosition table_from_record(const json& record) {
  TablePosition p;
  p.game = game_from_record(record);
  if (!record.contains("events") || !record["events"].is_array()) throw RecordError("record has no events");
  for (const json& event : record["events"]) apply_record_event(p, event);
  p.memory.sync(p.game);
  return p;
}

ExpertMove expert_move(const TablePosition& position, int seat, std::span<const ZooBot> opponents,
                       const ExpertSettings& settings, std::uint64_t seed) {
  const auto start = std::chrono::steady_clock::now();
  ExpertMove out;
  const auto finish = [&] {
    out.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return out;
  };
  const Game& game = position.game;
  if (game.game_over || seat < 0 || seat >= game.player_count) return finish();

  const ZooBot self = hard_bot();
  std::optional<Action> zoo_move = zoo_decide(game, seat, self, position.memory);
  if (!settings.search.trades && zoo_move && zoo_move->kind == ActionKind::kRespondTrade) zoo_move->accept = false;
  if (!zoo_move) return finish();  // nothing to do now
  const std::vector<Action> candidates = search_candidates(game, seat, zoo_move, settings.search.trades);
  if (candidates.size() <= 1) {
    out.action = candidates.empty() ? zoo_move : std::optional<Action>(candidates.front());
    return finish();
  }

  MctsSettings each = settings.search;
  const int threads = std::max(1, settings.threads);
  each.iterations = std::max(1, (settings.search.iterations + threads - 1) / threads);
  if (settings.time_limit.count() > 0) each.deadline = start + settings.time_limit;
  const std::uint64_t base =
      mix(mix(seed, static_cast<std::uint64_t>(position.events)), static_cast<std::uint64_t>(seat));
  const std::array<Cards, kMaxPlayers>& known = position.belief.known[at(seat)];

  std::vector<MctsSearch> parts(at(threads));
  std::vector<std::thread> pool;
  for (int w = 1; w < threads; ++w) {
    pool.emplace_back([&, w] {
      parts[at(w)] = mcts_search(game, seat, known, position.memory, self, opponents, each,
                                 mix(base, static_cast<std::uint64_t>(w)));
    });
  }
  parts[0] = mcts_search(game, seat, known, position.memory, self, opponents, each, mix(base, 0));
  for (std::thread& t : pool) t.join();
  for (int w = 1; w < threads; ++w) merge_searches(parts[0], parts[at(w)]);

  out.search = std::move(parts[0]);
  out.action = out.search.best;
  out.searched = true;
  return finish();
}

}  // namespace cae
