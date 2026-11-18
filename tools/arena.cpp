// catan_arena: matches between bots, with duplicate deals and SPRT
// (include/catan/arena.hpp explains both; docs/arena.md has results).
//
//   build/release/tools/catan_arena --candidate hard --opponents zoo --deals 500 --threads 16
//   build/release/tools/catan_arena --candidate zoo:ore-wheat/fair --opponents hard --sprt 0.25 0.30
//   build/release/tools/catan_arena --compare-modes --candidate hard --opponents zoo --deals 2000
//   build/release/tools/catan_arena --candidate hard --opponents catanatron --no-trades --deals 100
//       --python /tmp/catanatron/bin/python   (one command; split here to fit)
//
// Players:
//   hard                         the hard bot
//   zoo                          a new random zoo bot every deal (the same in its four games)
//   zoo:ARCHETYPE/PERSONALITY    that zoo bot, unshaken; add :SEED to shake its weights
//   elite:N                      elite N of the MAP-Elites archive (0 = best final score;
//                                --elites PATH, default data/elites.json)
//   elites                       a random elite every deal
// Zoo bots, hard and elites may end in +leader=X: the Robber's leader focus
// (zoo.hpp), for instance hard+leader=2.
//   catanatron                   Catanatron's AlphaBetaPlayer (depth 2) in its own process,
//                                through tools/catanatron_bridge.py; needs --python with
//                                Catanatron installed (docs/arena.md)
//   catanatron:weighted          Catanatron's WeightedRandomPlayer, a quick check of the bridge
// --opponents takes one player for all three seats or three, comma-separated.
//
// Modes: --mode plain (default) | rotate | duplicate. A deal is one game in
// plain mode and four games (the candidate in every seat) in the others.
// Measured on our bots, rotation and fixed dice do not make the result more
// precise per game (docs/arena.md), so plain is the default.
//
// --sprt P0 P1 stops as soon as the win rate is shown to be P1 rather than P0
// (or the other way round), checking after every --batch deals (default 64);
// --deals is then the most it will play. --alpha and --beta set the error
// rates (default 0.05 each).
//
// --compare-modes plays the same number of deals in each mode and prints how
// many games each needs for the same precision.
//
// Results do not depend on --threads: deal i always uses the same streams,
// and the SPRT looks at whole batches in deal order.

#include "catan/arena.hpp"
#include "catan/elites.hpp"
#include "catan/record.hpp"
#include "catan/replay.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using nlohmann::json;
constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// --- Catanatron in another process ------------------------------------------

// One Python process speaking JSON lines over a pair of pipes. Each thread of
// the arena owns one, so they never wait for each other.
class Bridge {
 public:
  Bridge(const std::string& python, const std::string& script, const std::string& player) {
    int to_child[2];
    int from_child[2];
    // Close-on-exec: a bridge started by another thread must not inherit
    // this one's pipe ends, or this Python never sees end of input and the
    // arena waits for it forever when it shuts down.
    if (pipe2(to_child, O_CLOEXEC) != 0 || pipe2(from_child, O_CLOEXEC) != 0) {
      throw std::runtime_error("pipe failed");
    }
    pid_ = fork();
    if (pid_ < 0) throw std::runtime_error("fork failed");
    if (pid_ == 0) {
      dup2(to_child[0], STDIN_FILENO);
      dup2(from_child[1], STDOUT_FILENO);
      close(to_child[0]);
      close(to_child[1]);
      close(from_child[0]);
      close(from_child[1]);
      execl(python.c_str(), python.c_str(), script.c_str(), player.c_str(), static_cast<char*>(nullptr));
      std::perror("exec of the Catanatron bridge");
      _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    in_ = fdopen(from_child[0], "r");
    out_ = fdopen(to_child[1], "w");
    if (in_ == nullptr || out_ == nullptr) throw std::runtime_error("fdopen failed");

    json hex_nodes = json::array();
    for (int h = 0; h < cae::kHexCount; ++h) {
      json corners = json::array();
      for (int d = 0; d < cae::kDirections; ++d) corners.push_back(cae::kTopology.hex_nodes[at(h)][at(d)]);
      hex_nodes.push_back(corners);
    }
    json topology = cae::topology_json();
    topology["hex_nodes"] = hex_nodes;
    const json hello = ask({{"type", "hello"}, {"topology", topology}});
    if (!hello.value("ok", false)) throw std::runtime_error("bridge refused hello: " + hello.dump());
  }
  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;
  ~Bridge() {
    if (out_ != nullptr) std::fclose(out_);
    if (in_ != nullptr) std::fclose(in_);
    if (pid_ > 0) waitpid(pid_, nullptr, 0);
  }

  json ask(const json& message) {
    const std::string line = message.dump() + "\n";
    if (std::fputs(line.c_str(), out_) < 0 || std::fflush(out_) != 0) throw std::runtime_error("bridge closed");
    std::string reply;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, in_) != nullptr) {
      reply += buffer;
      if (!reply.empty() && reply.back() == '\n') break;
    }
    if (reply.empty()) throw std::runtime_error("the Catanatron bridge exited (see its stderr above)");
    return json::parse(reply);
  }

 private:
  pid_t pid_ = -1;
  FILE* in_ = nullptr;
  FILE* out_ = nullptr;
};

struct BridgeStats {
  std::atomic<long> decisions{0};
  std::atomic<long> unmapped{0};  // Catanatron chose something our rules have no move for
};

// Catanatron as an arena player. Trades with players are declined here
// without asking (Catanatron does not trade between players); a steal uses
// the victim Catanatron named with its robber move.
cae::ArenaPlayer catanatron_player(Bridge& bridge, BridgeStats& stats) {
  auto victim = std::make_shared<int>(-1);
  return [&bridge, &stats, victim](const cae::ArenaView& v) -> std::optional<cae::Action> {
    const cae::Game& g = v.game;
    if (g.offer.open && g.offer.proposer != v.seat) {
      cae::Action no;
      no.kind = cae::ActionKind::kRespondTrade;
      no.accept = false;
      return no;
    }
    if (g.phase == cae::Phase::kSteal) {
      const std::uint8_t victims = g.steal_victims(v.seat);
      cae::Action a;
      a.kind = cae::ActionKind::kSteal;
      if (*victim >= 0 && ((victims >> *victim) & 1U) != 0) {
        a.target = *victim;
      } else {  // the victim with the most cards
        for (int s = 0; s < g.player_count; ++s) {
          if (((victims >> s) & 1U) != 0 &&
              (a.target < 0 || g.players[at(s)].card_count() > g.players[at(a.target)].card_count())) {
            a.target = s;
          }
        }
      }
      *victim = -1;
      return a;
    }
    json built = json::array();
    for (const cae::Built& b : v.built) built.push_back({static_cast<int>(b.kind), b.seat, b.at});
    const json reply = bridge.ask({{"type", "decide"},
                                   {"seat", v.seat},
                                   {"board", cae::board_json(g.board)},
                                   {"state", cae::snapshot(g)},
                                   {"legal", cae::legal_moves(g, v.seat)},
                                   {"built", built}});
    ++stats.decisions;
    if (reply.contains("victim") && reply["victim"].is_number_integer()) *victim = reply["victim"].get<int>();
    auto action = cae::action_from_json(reply.value("action", json()));
    if (!action) ++stats.unmapped;
    return action;
  };
}

// --- Players by name -----------------------------------------------------------

struct PlayerSpec {
  enum class Kind { kHard, kZooRandom, kZoo, kEliteRandom, kCatanatron } kind = Kind::kHard;
  cae::ZooBot bot;
  std::string bridge_player;  // "alphabeta" or "weighted"
  std::string name;
};

// The MAP-Elites archive, read once on first use.
std::string g_elites_path = "data/elites.json";
const std::vector<cae::ZooBot>& elites() {
  static const std::vector<cae::ZooBot> pool = cae::load_elites(g_elites_path);
  return pool;
}

PlayerSpec parse_player(const std::string& text) {
  PlayerSpec p;
  p.name = text;
  if (text == "zoo") {
    p.kind = PlayerSpec::Kind::kZooRandom;
    return p;
  }
  if (text == "elites") {
    (void)elites();
    p.kind = PlayerSpec::Kind::kEliteRandom;
    return p;
  }
  if (text == "catanatron" || text == "catanatron:weighted") {
    p.kind = PlayerSpec::Kind::kCatanatron;
    p.bridge_player = text == "catanatron" ? "alphabeta" : "weighted";
    return p;
  }
  // hard, zoo:ARCHETYPE/PERSONALITY[:SEED], elite:N, each with +leader=X
  p.kind = text.rfind("hard", 0) == 0 ? PlayerSpec::Kind::kHard : PlayerSpec::Kind::kZoo;
  p.bot = cae::zoo_bot_named(text, g_elites_path);
  return p;
}

std::vector<PlayerSpec> parse_players(const std::string& text) {
  std::vector<PlayerSpec> out;
  std::size_t start = 0;
  while (true) {
    const auto comma = text.find(',', start);
    out.push_back(parse_player(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (out.size() == 1) out.resize(3, out.front());
  if (out.size() != 3) throw std::runtime_error("--opponents takes one player or three");
  return out;
}

struct Options {
  std::string candidate = "hard";
  std::string baseline;  // empty: no pairing
  std::string opponents = "zoo";
  cae::DealMode mode = cae::DealMode::kPlain;
  int deals = 400;
  int threads = 1;
  std::uint64_t seed = 1;
  bool trades = true;
  bool sprt = false;
  cae::Sprt test;
  int batch = 64;
  bool compare = false;
  std::string python;
  std::string bridge_script;
};

// What one thread needs to seat players: its own bridge, if any.
struct Seating {
  const Options& o;
  PlayerSpec candidate;
  std::optional<PlayerSpec> baseline;
  std::vector<PlayerSpec> opponents;
  BridgeStats* stats = nullptr;
  // One bridge per kind of Catanatron player; one arena player per seat,
  // each with its own robber memory.
  std::map<std::string, std::unique_ptr<Bridge>> bridges;
  std::map<std::string, std::vector<cae::ArenaPlayer>> catanatron;

  cae::ArenaPlayer make(const PlayerSpec& p, std::uint64_t deal_seed, int slot, int seat) {
    switch (p.kind) {
      case PlayerSpec::Kind::kHard:
      case PlayerSpec::Kind::kZoo: return cae::zoo_player(p.bot, o.trades);
      case PlayerSpec::Kind::kZooRandom: {
        cae::Rng rng(deal_seed ^ (0x5eed0000ULL + static_cast<std::uint64_t>(slot)));
        return cae::zoo_player(cae::random_zoo_bot(rng), o.trades);
      }
      case PlayerSpec::Kind::kEliteRandom: {
        cae::Rng rng(deal_seed ^ (0x5eed0000ULL + static_cast<std::uint64_t>(slot)));
        return cae::zoo_player(elites()[at(rng.below(static_cast<int>(elites().size())))], o.trades);
      }
      case PlayerSpec::Kind::kCatanatron: {
        auto& bridge = bridges[p.bridge_player];
        if (!bridge) {
          if (o.python.empty()) throw std::runtime_error("catanatron players need --python");
          bridge = std::make_unique<Bridge>(o.python, o.bridge_script, p.bridge_player);
          for (int s = 0; s < cae::kMaxPlayers; ++s) {
            catanatron[p.bridge_player].push_back(catanatron_player(*bridge, *stats));
          }
        }
        return catanatron[p.bridge_player][at(seat)];
      }
    }
    return {};
  }

  // The table of one game: the candidate at `seat`, opponents in order around it.
  std::vector<cae::ArenaPlayer> table(const cae::Deal& deal, int seat, const PlayerSpec& who) {
    std::vector<cae::ArenaPlayer> out(cae::kMaxPlayers);
    int next = 0;
    for (int s = 0; s < cae::kMaxPlayers; ++s) {
      if (s == seat) {
        out[at(s)] = make(who, deal.seed, 99, s);
      } else {
        out[at(s)] = make(opponents[at(next)], deal.seed, next, s);
        ++next;
      }
    }
    return out;
  }
};

struct DealResult {
  double score = 0;  // share of the deal's games the candidate won
  int games = 0;
  int wins = 0;
  int unfinished = 0;
  long actions = 0;
  long turns = 0;
  int points = 0;     // the candidate's points, summed over the deal's games
  int fallbacks = 0;  // moves refused, all seats
};

DealResult play_deal(Seating& seating, int index, cae::DealMode mode, const PlayerSpec& who) {
  const cae::Deal deal = cae::make_deal(seating.o.seed, index);
  DealResult r;
  const auto play = [&](int seat, int game_number, bool fixed) {
    const auto table = seating.table(deal, seat, who);
    const cae::ArenaGame g = cae::play_arena_game(deal, table, game_number, fixed);
    ++r.games;
    if (g.winner == seat) ++r.wins;
    if (g.winner < 0) ++r.unfinished;
    r.actions += g.actions;
    r.turns += g.turns;
    r.points += g.points[at(seat)];
    for (int f : g.fallbacks) r.fallbacks += f;
  };
  if (mode == cae::DealMode::kPlain) {
    cae::Rng pick(deal.seed);
    play(pick.below(cae::kMaxPlayers), 0, false);
  } else {
    for (int seat = 0; seat < cae::kMaxPlayers; ++seat) play(seat, seat, mode == cae::DealMode::kDuplicate);
  }
  r.score = static_cast<double>(r.wins) / r.games;
  return r;
}

// Plays deals first .. last - 1 on the threads; results by deal index. With
// a baseline, each deal is also played by the baseline in the candidate's
// place: same board, deck, seats, opponents and game streams.
void play_deals(std::vector<std::unique_ptr<Seating>>& seatings, int first, int last, cae::DealMode mode,
                std::vector<DealResult>& out, std::vector<DealResult>* base = nullptr) {
  std::atomic<int> next{first};
  std::vector<std::thread> pool;
  std::vector<std::string> errors(seatings.size());
  for (std::size_t t = 0; t < seatings.size(); ++t) {
    pool.emplace_back([&, t] {
      try {
        Seating& seating = *seatings[t];
        for (int i = next++; i < last; i = next++) {
          out[at(i)] = play_deal(seating, i, mode, seating.candidate);
          if (base != nullptr) (*base)[at(i)] = play_deal(seating, i, mode, *seating.baseline);
        }
      } catch (const std::exception& e) {
        errors[t] = e.what();
        next = last;
      }
    });
  }
  for (auto& th : pool) th.join();
  for (const auto& e : errors) {
    if (!e.empty()) throw std::runtime_error(e);
  }
}

const char* mode_name(cae::DealMode m) {
  switch (m) {
    case cae::DealMode::kPlain: return "plain";
    case cae::DealMode::kRotate: return "rotate";
    case cae::DealMode::kDuplicate: return "duplicate";
  }
  return "?";
}

struct Summary {
  cae::ScoreStats stats;
  long games = 0;
  long wins = 0;
  long unfinished = 0;
  long actions = 0;
  long turns = 0;
  long points = 0;
  long fallbacks = 0;
  void add(const DealResult& r) {
    stats.add(r.score);
    games += r.games;
    wins += r.wins;
    unfinished += r.unfinished;
    actions += r.actions;
    turns += r.turns;
    points += r.points;
    fallbacks += r.fallbacks;
  }
  // Games needed for a 95% interval of +-1 percentage point: a deal of g
  // games with score variance v carries per-game variance g v.
  [[nodiscard]] double games_for_one_point() const {
    const double per_deal = static_cast<double>(games) / static_cast<double>(stats.n);
    return 1.96 * 1.96 * stats.variance() * per_deal / (0.01 * 0.01);
  }
};

void print_summary(const Options& o, const std::string& who, const Summary& s, double seconds) {
  const double rate = 100.0 * static_cast<double>(s.wins) / static_cast<double>(s.games);
  const double half = 100.0 * 1.96 * s.stats.std_error();
  std::printf("%s: %ld deals, %ld games in %.1f s (%.1f games/s), %.1f turns per game\n", mode_name(o.mode),
              s.stats.n, s.games, seconds, static_cast<double>(s.games) / seconds,
              static_cast<double>(s.turns) / static_cast<double>(s.games));
  std::printf("  %s wins %.1f%% +- %.1f (95%%), %.2f points per game; %ld unfinished, %ld refused moves\n",
              who.c_str(), rate, half, static_cast<double>(s.points) / static_cast<double>(s.games),
              s.unfinished, s.fallbacks);
}

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--candidate P] [--baseline P] [--opponents P[,P,P]] [--mode plain|rotate|duplicate] [--deals N]\n"
               "          [--threads T] [--seed S] [--no-trades] [--sprt P0 P1] [--alpha A] [--beta B] [--batch N]\n"
               "          [--compare-modes] [--python PATH] [--bridge PATH] [--elites PATH]\n",
               argv0);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  std::string self = argv[0];
  const auto slash = self.rfind('/');
  // The bridge script lives in tools/ of the source tree; the binary in build/<preset>/tools/.
  o.bridge_script = (slash == std::string::npos ? std::string(".") : self.substr(0, slash)) +
                    "/../../../tools/catanatron_bridge.py";
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--candidate") {
      o.candidate = value();
    } else if (flag == "--baseline") {
      o.baseline = value();
    } else if (flag == "--opponents") {
      o.opponents = value();
    } else if (flag == "--mode") {
      const std::string m = value();
      if (m == "plain") {
        o.mode = cae::DealMode::kPlain;
      } else if (m == "rotate") {
        o.mode = cae::DealMode::kRotate;
      } else if (m == "duplicate") {
        o.mode = cae::DealMode::kDuplicate;
      } else {
        usage(argv[0]);
      }
    } else if (flag == "--deals") {
      o.deals = std::atoi(value().c_str());
    } else if (flag == "--threads") {
      o.threads = std::max(1, std::atoi(value().c_str()));
    } else if (flag == "--seed") {
      o.seed = std::stoull(value());
    } else if (flag == "--no-trades") {
      o.trades = false;
    } else if (flag == "--sprt") {
      o.sprt = true;
      o.test.p0 = std::atof(value().c_str());
      o.test.p1 = std::atof(value().c_str());
    } else if (flag == "--alpha") {
      o.test.alpha = std::atof(value().c_str());
    } else if (flag == "--beta") {
      o.test.beta = std::atof(value().c_str());
    } else if (flag == "--batch") {
      o.batch = std::max(1, std::atoi(value().c_str()));
    } else if (flag == "--compare-modes") {
      o.compare = true;
    } else if (flag == "--python") {
      o.python = value();
    } else if (flag == "--bridge") {
      o.bridge_script = value();
    } else if (flag == "--elites") {
      g_elites_path = value();
    } else {
      usage(argv[0]);
    }
  }

  try {
    BridgeStats stats;
    std::vector<std::unique_ptr<Seating>> seatings;
    for (int t = 0; t < o.threads; ++t) {
      auto s = std::make_unique<Seating>(Seating{o, parse_player(o.candidate), std::nullopt,
                                                 parse_players(o.opponents), &stats, {}, {}});
      if (!o.baseline.empty()) s->baseline = parse_player(o.baseline);
      seatings.push_back(std::move(s));
    }
    const bool paired = !o.baseline.empty();
    std::printf("%s%s%s against %s, seed %llu%s\n", o.candidate.c_str(), paired ? " and " : "",
                o.baseline.c_str(), o.opponents.c_str(), static_cast<unsigned long long>(o.seed),
                o.trades ? "" : ", no trades between players");

    if (o.compare) {
      double plain_need = 0;
      for (const cae::DealMode mode : {cae::DealMode::kPlain, cae::DealMode::kRotate, cae::DealMode::kDuplicate}) {
        o.mode = mode;
        // Same number of games in each mode: plain deals are one game, the others four.
        const int deals = mode == cae::DealMode::kPlain ? o.deals * cae::kMaxPlayers : o.deals;
        std::vector<DealResult> results(at(deals));
        std::vector<DealResult> base(paired ? at(deals) : 0);
        const auto t0 = std::chrono::steady_clock::now();
        play_deals(seatings, 0, deals, mode, results, paired ? &base : nullptr);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        Summary s;
        for (const auto& r : results) s.add(r);
        print_summary(o, o.candidate, s, seconds);
        double need = s.games_for_one_point();
        if (paired) {
          Summary b;
          cae::ScoreStats diff;
          for (std::size_t i = 0; i < results.size(); ++i) {
            b.add(base[i]);
            diff.add(results[i].score - base[i].score);
          }
          print_summary(o, o.baseline, b, seconds);
          // Games (of both players together) for +-1 point on the difference:
          // paired, from the variance of the per-deal differences; unpaired,
          // as two separate matches whose variances add up.
          const double per_deal = 2.0 * static_cast<double>(s.games) / static_cast<double>(s.stats.n);
          const double z2 = 1.96 * 1.96 / (0.01 * 0.01);
          const double unpaired = z2 * (s.stats.variance() + b.stats.variance()) * per_deal;
          need = z2 * diff.variance() * per_deal;
          std::printf("  difference %+.1f points +- %.1f; games for +-1 point on it: %.0f paired, %.0f unpaired "
                      "(%.2fx)\n",
                      100 * diff.mean(), 100 * 1.96 * diff.std_error(), need, unpaired, unpaired / need);
        }
        if (mode == cae::DealMode::kPlain) plain_need = need;
        std::printf("  games for +-1 point (95%%): %.0f, %.2fx fewer than plain\n", need, plain_need / need);
      }
      return 0;
    }

    std::vector<DealResult> results(at(o.deals));
    std::vector<DealResult> base(paired ? at(o.deals) : 0);
    Summary s;
    Summary b;
    cae::ScoreStats diff;
    const auto t0 = std::chrono::steady_clock::now();
    cae::SprtResult verdict = cae::SprtResult::kContinue;
    int played = 0;
    while (played < o.deals && verdict == cae::SprtResult::kContinue) {
      const int last = o.sprt ? std::min(o.deals, played + o.batch) : o.deals;
      play_deals(seatings, played, last, o.mode, results, paired ? &base : nullptr);
      for (int i = played; i < last; ++i) {
        s.add(results[at(i)]);
        if (paired) {
          b.add(base[at(i)]);
          diff.add(results[at(i)].score - base[at(i)].score);
        }
      }
      played = last;
      if (o.sprt) verdict = cae::sprt_decide(o.test, paired ? diff : s.stats);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    print_summary(o, o.candidate, s, seconds);
    if (paired) {
      print_summary(o, o.baseline, b, seconds);
      std::printf("  difference %+.1f points +- %.1f (95%%, paired by deal)\n", 100 * diff.mean(),
                  100 * 1.96 * diff.std_error());
    }
    if (stats.decisions > 0) {
      std::printf("  Catanatron: %ld decisions, %ld it chose that our rules have no move for\n",
                  stats.decisions.load(), stats.unmapped.load());
    }
    if (o.sprt) {
      const cae::ScoreStats& tested = paired ? diff : s.stats;
      const char* word = verdict == cae::SprtResult::kH1   ? "H1 accepted"
                         : verdict == cae::SprtResult::kH0 ? "H0 accepted"
                                                           : "no decision yet";
      std::printf("  SPRT %s %.3f vs %.3f (alpha %.2f, beta %.2f): LLR %.2f in [%.2f, %.2f], %s after %ld deals\n",
                  paired ? "difference" : "win rate", o.test.p0, o.test.p1, o.test.alpha, o.test.beta,
                  o.test.llr(tested), o.test.lower(), o.test.upper(), word, tested.n);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "catan_arena: %s\n", e.what());
    return 1;
  }
  return 0;
}
