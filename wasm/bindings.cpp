// The engine as a WebAssembly module for the web viewer (web/).
//
// Embind exports a small API to JavaScript. Everything crosses the boundary
// as a string of JSON: the page parses it with JSON.parse, which is fast and
// keeps this file free of per-field glue. Positions are indices (hex 0..18,
// corner 0..53, side 0..71) and the page draws them from topologyJson().
//
//   const engine = await createEngine();
//   const replay = new engine.Replay(recordText);
//   replay.error();            // "" when every event applied
//   JSON.parse(replay.stateJson(42));
//   replay.delete();           // C++ memory is not garbage collected
//
// selfplay() runs the random player of catan_selfplay, single-threaded, to
// compare the engine in the browser with the native build; zooSelfplay()
// runs zoo bots, like `catan_selfplay --bots zoo`.
//
// Win chances (winrate.hpp): chancePointsJson() lists the positions to
// estimate, winChances(state, first, count, seed) plays playouts first ..
// first + count - 1 from one of them. Web Workers each open the record and
// take chunks, and the page adds the counts up.
//
// Setup review (setup_review.hpp): setupMovesJson() lists the settlements
// of the setup; setupReviewStep(move) runs one more round of that move's
// review (successive halving, then the confirmation) and answers where it
// stands. Each worker reviews whole moves, round by round, so the page can
// draw the heat map as it sharpens. Playouts seat the MAP-Elites given once
// to setElites() (the page bundles data/elites.json).
//
// Search (mcts.hpp): mctsDecisionsJson(count) lists that many decisions
// the searching bot would search, spread over the game; mctsSearch(k, count,
// iterations, seed, worker) runs worker `worker`'s part of decision k (its
// own seed, as catan_mcts --workers does), and the page merges the parts of
// its Web Workers at the root. Playouts seat the elites of setElites().
//
// Move review (review.hpp): reviewDecisionsJson(seat) lists the decisions of
// that seat the review grades; reviewMove(seat, k, seed, first, finalists, confirm) grades decision k
// (the page passes catan_review's budget: 32 playouts a move to start, 2
// finalists, 800 to confirm) and answers the review with its "why". Each worker grades whole
// decisions, so the move list fills in decision by decision.

#include "catan/elites.hpp"
#include "catan/mcts.hpp"
#include "catan/random_player.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"
#include "catan/review.hpp"
#include "catan/setup_review.hpp"
#include "catan/winrate.hpp"
#include "catan/zoo.hpp"

#include <emscripten/bind.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// The pool of every playout of the setup review; empty until setElites().
std::vector<cae::ZooBot>& elites() {
  static std::vector<cae::ZooBot> pool;
  return pool;
}

// "" on success, else why the archive was refused.
std::string setElites(const std::string& text) {
  try {
    elites() = cae::elites_from_json(nlohmann::json::parse(text));
    return "";
  } catch (const std::exception& e) {
    return e.what();
  }
}

nlohmann::json review_json(const cae::SetupReview& r) {
  nlohmann::json candidates = nlohmann::json::array();
  for (int c = 0; c < static_cast<int>(r.candidates().size()); ++c) {
    const cae::SetupCandidate& k = r.candidates()[static_cast<std::size_t>(c)];
    candidates.push_back({{"node", k.node},
                          {"value", r.value(c)},
                          {"playouts", r.value_playouts(c)},
                          {"dropped", k.dropped},
                          {"finalist", k.finalist},
                          {"halvingWins", k.halving.wins},
                          {"confirmWins", k.confirm.wins}});
  }
  nlohmann::json out = {{"done", r.done()},
                        {"rounds", r.rounds()},
                        {"playouts", r.playouts()},
                        {"seat", r.seat()},
                        {"played", r.played()},
                        {"best", r.best()},
                        {"candidates", candidates}};
  if (r.done()) {
    const cae::SetupReview::Loss l = r.loss();
    out["loss"] = {{"loss", l.loss}, {"error", l.error}};
  }
  return out;
}

class Replay {
 public:
  explicit Replay(const std::string& record_text)
      : log_(cae::ReplayLog::from_text(record_text)), text_(record_text) {}

  int eventCount() const { return log_.event_count(); }
  int stateCount() const { return log_.state_count(); }
  std::string error() const { return log_.error(); }
  // An index out of range gives "null", not an exception across the boundary.
  std::string stateJson(int i) const {
    if (i < 0 || i >= log_.state_count()) return "null";
    return log_.state_json(i).dump();
  }
  std::string boardJson() const { return log_.board_json().dump(); }

  std::string chancePointsJson() const {
    nlohmann::json out = nlohmann::json::array();
    for (const cae::ChancePoint& p : cae::chance_points(log_.states())) {
      out.push_back({{"state", p.state}, {"kind", static_cast<int>(p.kind)}, {"turn", p.turn}, {"seat", p.seat}});
    }
    return out.dump();
  }
  // The seed is a double: JavaScript numbers are exact up to 2^53.
  std::string winChances(int state, int first, int count, double seed) const {
    if (state < 0 || state >= log_.state_count() || count < 0) return "null";
    const cae::WinCounts w =
        cae::play_out_chances(log_.state(state), first, count, static_cast<std::uint64_t>(seed));
    const int players = log_.state(state).player_count;
    nlohmann::json wins = nlohmann::json::array();
    for (int s = 0; s < players; ++s) wins.push_back(w.wins[static_cast<std::size_t>(s)]);
    return nlohmann::json{{"wins", wins}, {"unfinished", w.unfinished}, {"playouts", w.playouts}}.dump();
  }

  std::string setupMovesJson() const {
    nlohmann::json out = nlohmann::json::array();
    for (const cae::SetupMove& m : cae::setup_moves(log_.states())) {
      out.push_back({{"state", m.state}, {"seat", m.seat}, {"node", m.node}, {"round", m.round}});
    }
    return out.dump();
  }
  // One more round of the review of setup move `move`, started on the first
  // call with that budget (catan_setup's --first, --finalists, --confirm;
  // the page passes their defaults, 100, 4 and 1600).
  std::string setupReviewStep(int move, double seed, int first, int finalists, int confirm) {
    const std::vector<cae::SetupMove> moves = cae::setup_moves(log_.states());
    if (move < 0 || move >= static_cast<int>(moves.size())) return "null";
    auto& review = reviews_[move];
    if (!review) {
      const cae::SetupMove& m = moves[static_cast<std::size_t>(move)];
      if (first < 1 || finalists < 1 || confirm < 1) return "null";
      cae::SetupReviewConfig config;
      config.first_playouts = first;
      config.finalists = finalists;
      config.confirm_playouts = confirm;
      config.seed = static_cast<std::uint64_t>(seed);
      review = std::make_unique<cae::SetupReview>(log_.state(m.state), m.node, config, elites());
    }
    review->step();
    return review_json(*review).dump();
  }

  std::string mctsDecisionsJson(int count) {
    nlohmann::json out = nlohmann::json::array();
    for (const cae::RecordDecision& d : decisions(count)) {
      out.push_back({{"state", d.state},
                     {"seat", d.seat},
                     {"phase", std::string(cae::phase_name(log_.state(d.state).phase))}});
    }
    return out.dump();
  }
  // Root statistics of one worker's search of decision k of `count`, and its time.
  std::string mctsSearch(int k, int count, int iterations, double seed, int worker, double c, double prior) {
    const std::vector<cae::RecordDecision>& list = decisions(count);
    if (k < 0 || k >= static_cast<int>(list.size()) || iterations < 1) return "null";
    cae::MctsSettings settings;
    settings.iterations = iterations;
    settings.exploration = c;
    settings.zoo_prior = prior;
    const cae::RecordDecision& d = list[static_cast<std::size_t>(k)];
    const auto t0 = std::chrono::steady_clock::now();
    const cae::MctsSearch s =
        cae::search_part(log_.state(d.state), d, elites(), settings, static_cast<std::uint64_t>(seed), worker);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    nlohmann::json moves = nlohmann::json::array();
    for (const cae::MctsMove& m : s.root) {
      moves.push_back({{"move", cae::action_text(m.action)},
                       {"kind", std::string(cae::action_name(m.action.kind))},
                       {"target", m.action.target},
                       {"prior", m.prior},
                       {"visits", m.visits},
                       {"wins", m.wins}});
    }
    return nlohmann::json{{"moves", moves}, {"iterations", s.iterations}, {"nodes", s.nodes}, {"seconds", seconds}}
        .dump();
  }

  // [{state, turn, kind}] of the decisions of `seat` the review grades;
  // {"failed": why} when the record does not replay. (Not "error": a review
  // has a field of that name, the standard error of its loss.)
  std::string reviewDecisionsJson(int seat) {
    try {
      nlohmann::json out = nlohmann::json::array();
      for (const cae::ReviewDecision& d : review_list(seat)) {
        out.push_back({{"state", d.state},
                       {"turn", d.game.turn_number},
                       {"kind", std::string(cae::action_name(d.played.kind))}});
      }
      return out.dump();
    } catch (const std::exception& e) {
      return nlohmann::json{{"failed", e.what()}}.dump();
    }
  }
  // The review of decision k of `seat`, with "why"; the same as catan_review
  // with --seed, --first, --finalists and --confirm (the page passes its
  // defaults, 32, 2 and 800) for that decision.
  std::string reviewMove(int seat, int k, double seed, int first, int finalists, int confirm) {
    try {
      const std::vector<cae::ReviewDecision>& list = review_list(seat);
      if (k < 0 || k >= static_cast<int>(list.size()) || first < 1 || finalists < 1 || confirm < 2) return "null";
      cae::ReviewConfig config;
      config.label.seed = static_cast<std::uint64_t>(seed);
      config.label.first_playouts = first;
      config.label.finalists = finalists;
      config.label.confirm_playouts = confirm;
      const cae::ReviewDecision& d = list[static_cast<std::size_t>(k)];
      const cae::MoveReview r = cae::review_decision(d, config, elites());
      nlohmann::json out = cae::review_json(r);
      out["why"] = cae::explain(d, r);
      return out.dump();
    } catch (const std::exception& e) {
      return nlohmann::json{{"failed", e.what()}}.dump();
    }
  }

 private:
  cae::ReplayLog log_;
  std::string text_;
  std::map<int, std::vector<cae::ReviewDecision>> review_lists_;

  const std::vector<cae::ReviewDecision>& review_list(int seat) {
    auto it = review_lists_.find(seat);
    if (it == review_lists_.end()) {
      it = review_lists_.emplace(seat, cae::review_decisions(nlohmann::json::parse(text_), seat)).first;
    }
    return it->second;
  }
  std::map<int, std::unique_ptr<cae::SetupReview>> reviews_;
  std::map<int, std::vector<cae::RecordDecision>> decisions_;

  const std::vector<cae::RecordDecision>& decisions(int count) {
    auto& list = decisions_[count];
    if (list.empty()) list = cae::spread_decisions(log_.states(), count);
    return list;
  }
};

std::string topologyJson() { return cae::topology_json().dump(); }

// The same games as `catan_selfplay --games N --players P --seed S`, so the
// checksum must match the native one: that proves the WebAssembly build plays
// the same moves, not just that it runs.
std::string play_games(int games, int players, double seed_number, bool zoo) {
  const auto seed = static_cast<std::uint64_t>(seed_number);
  long long actions = 0;
  long long refused = 0;
  std::uint64_t checksum = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < games; ++i) {
    // The same streams and the same order of draws as tools/selfplay.cpp
    cae::Rng rng((seed << 32) ^ static_cast<std::uint64_t>(i));
    cae::PlayoutResult r;
    if (zoo) {
      cae::ZooTable table;
      table.game = cae::random_game(rng, players);
      for (int s = 0; s < players; ++s) table.bots[static_cast<std::size_t>(s)] = cae::random_zoo_bot(rng);
      const cae::ZooPlayout z = cae::play_out_zoo(table, rng);
      r = {z.winner, z.actions, z.fallbacks};
    } else {
      cae::Game game = cae::random_game(rng, players);
      r = cae::play_out(game, rng);
    }
    actions += r.actions;
    refused += r.refused;
    checksum += static_cast<std::uint64_t>(r.actions) * 31 + static_cast<std::uint64_t>(r.winner + 1);
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  // The checksum is a string: a 64-bit integer does not fit a JS number.
  nlohmann::json out = {{"games", games},
                        {"actions", actions},
                        {"refused", refused},
                        {"checksum", std::to_string(checksum)},
                        {"seconds", seconds}};
  return out.dump();
}

std::string selfplay(int games, int players, double seed) { return play_games(games, players, seed, false); }
std::string zooSelfplay(int games, int players, double seed) { return play_games(games, players, seed, true); }

}  // namespace

EMSCRIPTEN_BINDINGS(catan_engine) {
  emscripten::class_<Replay>("Replay")
      .constructor<const std::string&>()
      .function("eventCount", &Replay::eventCount)
      .function("stateCount", &Replay::stateCount)
      .function("error", &Replay::error)
      .function("stateJson", &Replay::stateJson)
      .function("boardJson", &Replay::boardJson)
      .function("chancePointsJson", &Replay::chancePointsJson)
      .function("winChances", &Replay::winChances)
      .function("setupMovesJson", &Replay::setupMovesJson)
      .function("setupReviewStep", &Replay::setupReviewStep)
      .function("mctsDecisionsJson", &Replay::mctsDecisionsJson)
      .function("mctsSearch", &Replay::mctsSearch)
      .function("reviewDecisionsJson", &Replay::reviewDecisionsJson)
      .function("reviewMove", &Replay::reviewMove);
  emscripten::function("topologyJson", &topologyJson);
  emscripten::function("setElites", &setElites);
  emscripten::function("selfplay", &selfplay);
  emscripten::function("zooSelfplay", &zooSelfplay);
}
