#include "catan/elites.hpp"

#include <fstream>
#include <stdexcept>

namespace cae {

using nlohmann::json;

json zoo_bot_json(const ZooBot& bot) {
  const ZooWeights& w = bot.weights;
  const ZooTrading& t = bot.trading;
  json resource = json::array();
  for (const double r : w.resource) resource.push_back(r);
  return {{"origin", std::string(archetype_name(bot.archetype)) + "/" + std::string(personality_name(bot.personality))},
          {"weights",
           {{"resource", resource},
            {"variety", w.variety},
            {"port", w.port},
            {"city", w.city},
            {"settlement", w.settlement},
            {"dev_card", w.dev_card},
            {"road_reach", w.road_reach},
            {"leader_focus", w.leader_focus}}},
          {"trading",
           {{"offers_per_turn", t.offers_per_turn},
            {"accepts", t.accepts},
            {"neutral", t.neutral},
            {"embargo", t.embargo}}}};
}

ZooBot zoo_bot_from_json(const json& j) {
  ZooBot bot;
  const std::string origin = j.value("origin", std::string("balanced/fair"));
  const auto slash = origin.find('/');
  for (int i = 0; i < kArchetypeCount; ++i) {
    if (origin.substr(0, slash) == archetype_name(static_cast<Archetype>(i))) bot.archetype = static_cast<Archetype>(i);
  }
  if (slash != std::string::npos) {
    for (int i = 0; i < kPersonalityCount; ++i) {
      if (origin.substr(slash + 1) == personality_name(static_cast<Personality>(i))) {
        bot.personality = static_cast<Personality>(i);
      }
    }
  }
  const json& w = j.at("weights");
  for (std::size_t r = 0; r < bot.weights.resource.size(); ++r) {
    bot.weights.resource[r] = w.at("resource").at(r).get<double>();
  }
  bot.weights.variety = w.at("variety").get<double>();
  bot.weights.port = w.at("port").get<double>();
  bot.weights.city = w.at("city").get<double>();
  bot.weights.settlement = w.at("settlement").get<double>();
  bot.weights.dev_card = w.at("dev_card").get<double>();
  bot.weights.road_reach = w.at("road_reach").get<int>();
  bot.weights.leader_focus = w.value("leader_focus", 0.0);
  const json& t = j.at("trading");
  bot.trading.offers_per_turn = t.at("offers_per_turn").get<int>();
  bot.trading.accepts = t.at("accepts").get<bool>();
  bot.trading.neutral = t.at("neutral").get<bool>();
  bot.trading.embargo = t.at("embargo").get<int>();
  return bot;
}

json evaluation_json(const Evaluation& e) {
  const Behavior b = e.behavior();
  return {{"games", e.games},
          {"wins", e.wins},
          {"unfinished", e.unfinished},
          {"win_rate", e.win_rate()},
          {"margin", e.margin()},
          {"city_share", b.city_share},
          {"trades", b.trades},
          {"dev_cards", b.dev_cards}};
}

Evaluation evaluation_from_json(const json& j) {
  Evaluation e;
  e.games = j.value("games", 0L);
  e.wins = j.value("wins", 0L);
  e.unfinished = j.value("unfinished", 0L);
  const auto n = static_cast<double>(e.games);
  e.city_share = j.value("city_share", 0.0) * n;
  e.trades = j.value("trades", 0.0) * n;
  e.dev_cards = j.value("dev_cards", 0.0) * n;
  return e;
}

ZooBot zoo_bot_named(std::string name, const std::string& elites_path) {
  const std::string original = name;
  double leader = -1;
  if (const auto plus = name.find("+leader="); plus != std::string::npos) {
    leader = std::stod(name.substr(plus + 8));
    name = name.substr(0, plus);
  }
  if (name.rfind("zoo:", 0) == 0) name = name.substr(4);
  ZooBot bot;
  if (name == "hard") {
    bot = hard_bot();
  } else if (name.rfind("elite:", 0) == 0) {
    const auto elites = load_elites(elites_path);
    const auto n = std::stoul(name.substr(6));
    if (n >= elites.size()) {
      throw std::runtime_error(original + ": " + elites_path + " has " + std::to_string(elites.size()) + " elites");
    }
    bot = elites[n];
  } else {
    std::uint64_t seed = 0;
    if (const auto colon = name.find(':'); colon != std::string::npos) {
      seed = std::stoull(name.substr(colon + 1));
      name = name.substr(0, colon);
    }
    const auto slash = name.find('/');
    const std::string archetype = name.substr(0, slash);
    const std::string personality = slash == std::string::npos ? "fair" : name.substr(slash + 1);
    int a = -1;
    int p = -1;
    for (int i = 0; i < kArchetypeCount; ++i) {
      if (archetype == archetype_name(static_cast<Archetype>(i))) a = i;
    }
    for (int i = 0; i < kPersonalityCount; ++i) {
      if (personality == personality_name(static_cast<Personality>(i))) p = i;
    }
    if (a < 0 || p < 0) throw std::runtime_error("unknown bot: " + original);
    bot = make_zoo_bot(static_cast<Archetype>(a), static_cast<Personality>(p), seed);
  }
  if (leader >= 0) bot.weights.leader_focus = leader;
  return bot;
}

std::vector<ZooBot> load_elites(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  return elites_from_json(json::parse(in));
}

std::vector<ZooBot> elites_from_json(const json& j) {
  std::vector<ZooBot> out;
  for (const json& e : j.at("elites")) out.push_back(zoo_bot_from_json(e.at("bot")));
  if (out.empty()) throw std::runtime_error("the archive holds no elites");
  return out;
}

}  // namespace cae
