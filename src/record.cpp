#include "catan/record.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <vector>

namespace cae {

using nlohmann::json;

namespace {

constexpr std::size_t at(int i) noexcept { return static_cast<std::size_t>(i); }

// Mirrors the parsing in catan-project's app/services/actions.py: ints must
// be real ints (not bools, not 1.0), cube coordinates must sum to zero,
// directions are 0..5. Anything else is a refusal, not a crash.
constexpr int kMaxCardsInOneAction = 200;

bool is_int(const json& j) { return j.is_number_integer(); }

std::optional<Cube> parse_hex(const json& j) {
  if (!j.is_object()) return std::nullopt;
  for (const char* k : {"q", "r", "s"}) {
    if (!j.contains(k) || !is_int(j[k])) return std::nullopt;
  }
  const auto q = j["q"].get<long long>();
  const auto r = j["r"].get<long long>();
  const auto s = j["s"].get<long long>();
  if (q + r + s != 0) return std::nullopt;
  // Far away is still a valid position; it is just not on the board.
  constexpr long long kFar = 1000;
  if (q < -kFar || q > kFar || r < -kFar || r > kFar) return Cube{kFar, kFar};
  return Cube{static_cast<int>(q), static_cast<int>(r)};
}

// A corner or a side: {"hex": {...}, "direction": d}. Returns the index from
// `lookup`, -1 when it is off the board, or nullopt when malformed.
template <typename Lookup>
std::optional<int> parse_position(const json& payload, Lookup lookup) {
  if (!payload.is_object() || !payload.contains("hex") || !payload.contains("direction")) return std::nullopt;
  const auto hex = parse_hex(payload["hex"]);
  const json& d = payload["direction"];
  if (!hex || !is_int(d)) return std::nullopt;
  const auto direction = d.get<long long>();
  if (direction < 0 || direction > 5) return std::nullopt;
  return lookup(hex->q, hex->r, static_cast<int>(direction));
}

std::optional<Resource> parse_resource(const json& j) {
  if (!j.is_string()) return std::nullopt;
  const auto& name = j.get_ref<const std::string&>();
  for (int r = 0; r < kResourceCount; ++r) {
    if (name == resource_name(static_cast<Resource>(r))) return static_cast<Resource>(r);
  }
  return std::nullopt;  // includes "desert", which the platform cannot trade either
}

std::optional<Cards> parse_cards(const json& j) {
  if (!j.is_object()) return std::nullopt;
  Cards cards{};
  for (const auto& [name, amount] : j.items()) {
    if (!is_int(amount)) return std::nullopt;
    const auto n = amount.get<long long>();
    if (n > kMaxCardsInOneAction || n < -kMaxCardsInOneAction) return std::nullopt;
    if (n == 0) continue;  // zero entries are dropped before the name is read
    const auto r = parse_resource(json(name));
    if (!r) return std::nullopt;
    cards[at(static_cast<int>(*r))] = static_cast<int>(n);
  }
  return cards;
}

std::optional<int> parse_seat(const json& j) {
  if (!is_int(j)) return std::nullopt;
  const auto seat = j.get<long long>();
  if (seat < 0 || seat >= kMaxPlayers) return -1;
  return static_cast<int>(seat);
}

std::optional<Terrain> parse_terrain(const std::string& name) {
  static constexpr std::array<const char*, 6> kNames = {"wood", "brick", "sheep", "wheat", "ore", "desert"};
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (name == kNames[i]) return static_cast<Terrain>(i);
  }
  return std::nullopt;
}

std::optional<PortType> parse_port(const std::string& name) {
  static constexpr std::array<const char*, 6> kNames = {"3:1",       "wood_2:1",  "brick_2:1",
                                                        "sheep_2:1", "wheat_2:1", "ore_2:1"};
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (name == kNames[i]) return static_cast<PortType>(i);
  }
  return std::nullopt;
}

std::optional<DevCard> parse_dev_card(const std::string& name) {
  for (int c = 0; c < kDevCardKinds; ++c) {
    if (name == dev_card_name(static_cast<DevCard>(c))) return static_cast<DevCard>(c);
  }
  return std::nullopt;
}

const json& field(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key)) throw RecordError(std::string("missing field: ") + key);
  return j[key];
}

json cards_json(const Cards& cards) {
  json out = json::array();
  for (int n : cards) out.push_back(n);
  return out;
}

json seats_json(std::uint8_t mask, int players) {
  json out = json::array();
  for (int s = 0; s < players; ++s) {
    if ((mask >> s) & 1U) out.push_back(s);
  }
  return out;
}

json nodes_json(NodeMask mask) {
  json out = json::array();
  for_each_node(mask, [&](int n) { out.push_back(n); });
  return out;
}

json edges_json(EdgeMask mask) {
  json out = json::array();
  for_each_edge(mask, [&](int e) { out.push_back(e); });
  return out;
}

json optional_seat(int seat) { return seat >= 0 ? json(seat) : json(nullptr); }

Game start_from(const json& record);

}  // namespace

Game game_from_record(const json& record) {
  try {
    return start_from(record);
  } catch (const json::exception& e) {
    // A field of the wrong type (a string where a number belongs, ...).
    throw RecordError(std::string("broken header: ") + e.what());
  }
}

namespace {

Game start_from(const json& record) {
  if (!record.is_object() || record.value("format", "") != "catan-game-record") {
    throw RecordError("not a game record");
  }
  if (!record.contains("version") || record["version"] != 1) {
    throw RecordError("unsupported record version; this code reads 1");
  }

  Settings settings;
  if (record.contains("settings") && record["settings"].is_object()) {
    const json& s = record["settings"];
    settings.victory_points = s.value("victory_points", settings.victory_points);
    settings.discard_limit = s.value("discard_limit", settings.discard_limit);
    settings.friendly_robber = s.value("friendly_robber", settings.friendly_robber);
  }

  const json& players = field(record, "players");
  if (!players.is_array() || players.size() < 2 || players.size() > kMaxPlayers) {
    throw RecordError("a game has 2 to 4 players");
  }
  const int player_count = static_cast<int>(players.size());
  const int first_seat = record.value("first_seat", 0);
  if (first_seat < 0 || first_seat >= player_count) throw RecordError("first seat is not at the table");

  Board board;
  std::array<bool, kHexCount> seen{};
  const json& tiles = field(field(record, "board"), "tiles");
  if (!tiles.is_array() || tiles.size() != kHexCount) throw RecordError("the board must have 19 tiles");
  for (const json& tile : tiles) {
    const auto hex = parse_hex(field(tile, "hex"));
    const int h = hex ? hex_at(hex->q, hex->r) : -1;
    const auto terrain = parse_terrain(field(tile, "resource").get<std::string>());
    if (h < 0 || seen[at(h)] || !terrain) throw RecordError("tiles are not the standard 19 hexes");
    seen[at(h)] = true;
    board.terrain[at(h)] = *terrain;
    const json& number = field(tile, "number");
    board.number[at(h)] = static_cast<std::int8_t>(number.is_null() ? 0 : number.get<int>());
  }
  const json& ports = field(field(record, "board"), "ports");
  if (!ports.is_array() || ports.size() > kMaxPorts) throw RecordError("too many ports");
  for (const json& port : ports) {
    const auto type = parse_port(field(port, "type").get<std::string>());
    if (!type) throw RecordError("unknown port type");
    Port p{*type, 0};
    for (const json& vertex : field(port, "vertices")) {
      const auto node = parse_position(vertex, node_at);
      if (!node || *node < 0) throw RecordError("port corner is not on the board");
      p.nodes |= bit(*node);
    }
    board.ports[at(board.port_count++)] = p;
  }

  std::vector<DevCard> deck;
  for (const json& card : field(field(record, "dev_deck"), "cards")) {
    const auto c = parse_dev_card(card.get<std::string>());
    if (!c) throw RecordError("unknown development card");
    deck.push_back(*c);
  }
  if (deck.size() > kDevDeckSize) throw RecordError("the deck has more than 25 cards");

  Game game = Game::create(board, settings, player_count, first_seat, deck);
  const auto robber = parse_hex(field(field(record, "board"), "robber"));
  if (!robber || hex_at(robber->q, robber->r) != game.robber_hex) {
    throw RecordError("the robber does not start on the desert");
  }
  return game;
}

}  // namespace

Error apply_event(Game& game, const json& event) {
  if (!event.is_object() || !event.contains("action") || !event["action"].is_string()) {
    return Error::kBadArgument;
  }
  const auto seat_parsed = event.contains("seat") ? parse_seat(event["seat"]) : std::nullopt;
  if (!seat_parsed || *seat_parsed < 0 || *seat_parsed >= game.player_count) return Error::kBadArgument;
  const int seat = *seat_parsed;
  const std::string& action = event["action"].get_ref<const std::string&>();
  static const json kEmpty = json::object();
  const json& payload = event.contains("payload") ? event["payload"] : kEmpty;
  const json& outcome = event.contains("outcome") ? event["outcome"] : kEmpty;
  if (!payload.is_object()) return Error::kBadArgument;

  const auto get = [&payload](const char* key) -> const json& {
    static const json kNull;
    return payload.contains(key) ? payload[key] : kNull;
  };

  if (action == "roll_dice") {
    if (!outcome.is_object() || !outcome.contains("dice")) return Error::kBadOutcome;
    const json& dice = outcome["dice"];
    if (!dice.is_array() || dice.size() != 2 || !is_int(dice[0]) || !is_int(dice[1])) return Error::kBadOutcome;
    return game.roll_dice(seat, dice[0].get<int>(), dice[1].get<int>());
  }
  if (action == "end_turn") return game.end_turn(seat);
  if (action == "build_settlement" || action == "upgrade_city") {
    const auto node = parse_position(payload, node_at);
    if (!node) return Error::kBadArgument;
    return action == "build_settlement" ? game.build_settlement(seat, *node) : game.upgrade_city(seat, *node);
  }
  if (action == "build_road") {
    const auto edge = parse_position(payload, edge_at);
    if (!edge) return Error::kBadArgument;
    return game.build_road(seat, *edge);
  }
  if (action == "discard") {
    const auto cards = parse_cards(get("cards"));
    if (!cards) return Error::kBadArgument;
    return game.discard(seat, *cards);
  }
  if (action == "move_robber") {
    const auto hex = parse_hex(get("hex"));
    if (!hex) return Error::kBadArgument;
    return game.move_robber(seat, hex_at(hex->q, hex->r));
  }
  if (action == "steal") {
    const auto victim = parse_seat(get("victim_seat"));
    if (!victim || *victim < 0 || *victim >= game.player_count) return Error::kBadArgument;
    const auto taken = outcome.is_object() && outcome.contains("resource")
                           ? parse_resource(outcome["resource"])
                           : std::nullopt;
    if (!taken) {
      // Without a recorded card the steal can still be refused by the rules;
      // only a steal the rules would allow needs the card.
      Game copy = game;
      const Error e = copy.steal(seat, *victim, Resource::kWood);
      return e == Error::kNone ? Error::kBadOutcome : e;
    }
    return game.steal(seat, *victim, *taken);
  }
  if (action == "offer_trade") {
    const auto give = parse_cards(get("give"));
    const auto want = parse_cards(get("get"));
    if (!give || !want) return Error::kBadArgument;
    return game.offer_trade(seat, *give, *want);
  }
  if (action == "respond_trade") {
    const json& accept = get("accept");
    return game.respond_trade(seat, accept.is_boolean() && accept.get<bool>());
  }
  if (action == "confirm_trade") {
    const auto partner = parse_seat(get("partner_seat"));
    if (!partner || *partner < 0 || *partner >= game.player_count) return Error::kBadArgument;
    return game.confirm_trade(seat, *partner);
  }
  if (action == "cancel_trade") return game.cancel_trade(seat);
  if (action == "buy_dev_card") {
    const int next = game.dev_deck_next;
    const Error e = game.buy_dev_card(seat);
    // The deck order is in the header, so the drawn card is known; a record
    // naming a different one is broken.
    if (e == Error::kNone && outcome.is_object() && outcome.contains("card") &&
        outcome["card"] != std::string(dev_card_name(game.dev_deck[at(next)]))) {
      return Error::kBadOutcome;
    }
    return e;
  }
  if (action == "play_knight") return game.play_knight(seat);
  if (action == "play_road_building") return game.play_road_building(seat);
  if (action == "play_year_of_plenty") {
    const auto first = parse_resource(get("first"));
    const auto second = parse_resource(get("second"));
    if (!first || !second) return Error::kBadArgument;
    return game.play_year_of_plenty(seat, *first, *second);
  }
  if (action == "play_monopoly") {
    const auto r = parse_resource(get("resource"));
    if (!r) return Error::kBadArgument;
    return game.play_monopoly(seat, *r);
  }
  if (action == "trade_with_bank") {
    const auto give = parse_resource(get("give"));
    const auto want = parse_resource(get("get"));
    if (!give || !want) return Error::kBadArgument;
    return game.trade_with_bank(seat, *give, *want);
  }
  return Error::kBadArgument;
}

json snapshot(const Game& g) {
  json players = json::array();
  for (int s = 0; s < g.player_count; ++s) {
    const Player& p = g.players[at(s)];
    json dev = json::array();
    json fresh = json::array();
    for (int c = 0; c < kDevCardKinds; ++c) {
      dev.push_back(p.dev_cards[at(c)]);
      fresh.push_back(p.new_dev_cards[at(c)]);
    }
    players.push_back({
        {"resources", cards_json(p.resources)},
        {"points", g.visible_points(s)},
        {"hidden_points", g.hidden_points(s)},
        {"dev_cards", dev},
        {"new_dev_cards", fresh},
        {"knights", p.knights},
        {"settlements", nodes_json(p.settlements)},
        {"cities", nodes_json(p.cities)},
        {"roads", edges_json(p.roads)},
    });
  }
  json queue = json::array();
  for (int i = g.setup_position; i < g.setup_length; ++i) queue.push_back(g.setup_queue[at(i)]);
  json pending = json::array();
  for (int s = 0; s < g.player_count; ++s) {
    const int n = g.pending_discards[at(s)];
    pending.push_back(n >= 0 ? json(n) : json(nullptr));
  }
  json offer = nullptr;
  if (g.offer.open) {
    offer = {{"proposer", static_cast<int>(g.offer.proposer)},
             {"give", cards_json(g.offer.give)},
             {"get", cards_json(g.offer.get)},
             {"accepted", seats_json(g.offer.accepted, g.player_count)},
             {"declined", seats_json(g.offer.declined, g.player_count)}};
  }
  json deck = json::array();
  for (int i = g.dev_deck_next; i < g.dev_deck_size; ++i) deck.push_back(dev_card_name(g.dev_deck[at(i)]));
  json lengths = json::array();
  for (int s = 0; s < g.player_count; ++s) lengths.push_back(g.road_lengths[at(s)]);

  return {
      {"players", players},
      {"current", g.current},
      {"turn", g.turn_number},
      {"phase", phase_name(g.phase)},
      {"dice_roll", g.dice_roll > 0 ? json(g.dice_roll) : json(nullptr)},
      {"roll_counts", g.roll_counts},
      {"setup_queue", queue},
      {"setup_waiting_for_road", g.setup_waiting_for_road},
      {"setup_last_node", g.setup_last_node >= 0 ? json(g.setup_last_node) : json(nullptr)},
      {"robber", g.robber_hex},
      {"pending_discards", pending},
      {"return_phase", phase_name(g.return_phase)},
      {"free_roads_left", g.free_roads_left},
      {"offer", offer},
      {"dev_deck", deck},
      {"dev_card_played", g.dev_card_played},
      {"largest_army", optional_seat(g.largest_army)},
      {"longest_road", optional_seat(g.longest_road)},
      {"road_lengths", lengths},
      {"game_over", g.game_over},
      {"winner", optional_seat(g.winner)},
  };
}

json legal_moves(const Game& g, int seat) {
  json hexes = json::array();
  const std::uint32_t robber = g.legal_robber_hexes(seat);
  for (int h = 0; h < kHexCount; ++h) {
    if ((robber >> h) & 1U) hexes.push_back(h);
  }
  json cards = json::array();
  const std::uint8_t playable = g.playable_dev_cards(seat);
  for (int c = 0; c < kDevCardKinds; ++c) {
    if ((playable >> c) & 1U) cards.push_back(dev_card_name(static_cast<DevCard>(c)));
  }
  const bool robbing = !g.game_over && seat == g.current && g.phase == Phase::kSteal;
  return {
      {"settlements", nodes_json(g.legal_settlements(seat))},
      {"roads", edges_json(g.legal_roads(seat))},
      {"cities", nodes_json(g.legal_cities(seat))},
      {"robber_hexes", hexes},
      {"steal_victims", robbing ? seats_json(g.steal_victims(seat), g.player_count) : json::array()},
      {"playable_dev_cards", cards},
      {"can_buy_dev_card", g.can_buy_dev_card(seat)},
  };
}

std::optional<Action> action_from_event(const json& event) {
  if (!event.is_object() || !event.contains("action") || !event["action"].is_string()) return std::nullopt;
  const std::string& name = event["action"].get_ref<const std::string&>();
  static const json kEmpty = json::object();
  const json& payload = event.contains("payload") && event["payload"].is_object() ? event["payload"] : kEmpty;
  const auto get = [&payload](const char* key) -> const json& {
    static const json kNull;
    return payload.contains(key) ? payload[key] : kNull;
  };
  Action a;
  bool found = false;
  for (int k = 0; k <= static_cast<int>(ActionKind::kEndTurn); ++k) {
    if (name == action_name(static_cast<ActionKind>(k))) {
      a.kind = static_cast<ActionKind>(k);
      found = true;
    }
  }
  if (!found) return std::nullopt;

  const auto position = [&](auto lookup) -> bool {
    const auto p = parse_position(payload, lookup);
    if (!p || *p < 0) return false;
    a.target = *p;
    return true;
  };
  const auto resource = [&](const char* key, Resource& out) -> bool {
    const auto r = parse_resource(get(key));
    if (r) out = *r;
    return r.has_value();
  };
  const auto cards = [&](const char* key, Cards& out) -> bool {
    const auto c = parse_cards(get(key));
    if (c) out = *c;
    return c.has_value();
  };
  const auto seat = [&](const char* key) -> bool {
    const auto s = parse_seat(get(key));
    if (s) a.target = *s;
    return s.has_value();
  };

  bool ok = true;
  switch (a.kind) {
    case ActionKind::kBuildSettlement:
    case ActionKind::kUpgradeCity: ok = position(node_at); break;
    case ActionKind::kBuildRoad: ok = position(edge_at); break;
    case ActionKind::kMoveRobber: {
      const auto hex = parse_hex(get("hex"));
      ok = hex && hex_at(hex->q, hex->r) >= 0;
      if (ok) a.target = hex_at(hex->q, hex->r);
      break;
    }
    case ActionKind::kSteal: ok = seat("victim_seat"); break;
    case ActionKind::kConfirmTrade: ok = seat("partner_seat"); break;
    case ActionKind::kDiscard: ok = cards("cards", a.give); break;
    case ActionKind::kOfferTrade: ok = cards("give", a.give) && cards("get", a.get); break;
    case ActionKind::kRespondTrade: a.accept = get("accept").is_boolean() && get("accept").get<bool>(); break;
    case ActionKind::kTradeWithBank: ok = resource("give", a.first) && resource("get", a.second); break;
    case ActionKind::kPlayYearOfPlenty: ok = resource("first", a.first) && resource("second", a.second); break;
    case ActionKind::kPlayMonopoly: ok = resource("resource", a.first); break;
    default: break;
  }
  if (!ok) return std::nullopt;
  return a;
}

json event_from_action(const Action& a) {
  const auto hex = [](Cube c) { return json{{"q", c.q}, {"r", c.r}, {"s", -c.q - c.r}}; };
  const auto spelled = [&](const Spelling& sp) { return json{{"hex", hex(sp.hex)}, {"direction", sp.direction}}; };
  const auto cards = [](const Cards& c) {
    json out = json::object();
    for (int r = 0; r < kResourceCount; ++r) {
      if (c[at(r)] != 0) out[std::string(resource_name(static_cast<Resource>(r)))] = c[at(r)];
    }
    return out;
  };
  json payload = json::object();
  switch (a.kind) {
    case ActionKind::kBuildSettlement:
    case ActionKind::kUpgradeCity: payload = spelled(kTopology.node_spelling[at(a.target)]); break;
    case ActionKind::kBuildRoad: payload = spelled(kTopology.edge_spelling[at(a.target)]); break;
    case ActionKind::kMoveRobber: payload["hex"] = hex(kTopology.hex_cube[at(a.target)]); break;
    case ActionKind::kSteal: payload["victim_seat"] = a.target; break;
    case ActionKind::kConfirmTrade: payload["partner_seat"] = a.target; break;
    case ActionKind::kDiscard: payload["cards"] = cards(a.give); break;
    case ActionKind::kOfferTrade:
      payload["give"] = cards(a.give);
      payload["get"] = cards(a.get);
      break;
    case ActionKind::kRespondTrade: payload["accept"] = a.accept; break;
    case ActionKind::kTradeWithBank:
      payload["give"] = resource_name(a.first);
      payload["get"] = resource_name(a.second);
      break;
    case ActionKind::kPlayYearOfPlenty:
      payload["first"] = resource_name(a.first);
      payload["second"] = resource_name(a.second);
      break;
    case ActionKind::kPlayMonopoly: payload["resource"] = resource_name(a.first); break;
    default: break;
  }
  return json{{"action", action_name(a.kind)}, {"payload", std::move(payload)}};
}

std::optional<ZooBot> zoo_bot_from_record(const json& player) {
  if (!player.is_object() || !player.contains("bot_level") || !player["bot_level"].is_string()) {
    return std::nullopt;
  }
  const std::string& level = player["bot_level"].get_ref<const std::string&>();
  if (level == "hard") return hard_bot();
  if (level != "zoo" || !player.contains("bot_style")) return std::nullopt;
  const json& style = player["bot_style"];
  ZooBot bot;
  for (int a = 0; a < kArchetypeCount; ++a) {
    if (style.value("archetype", "") == archetype_name(static_cast<Archetype>(a))) bot.archetype = static_cast<Archetype>(a);
  }
  for (int p = 0; p < kPersonalityCount; ++p) {
    if (style.value("personality", "") == personality_name(static_cast<Personality>(p))) {
      bot.personality = static_cast<Personality>(p);
    }
  }
  bot.seed = style.value("seed", std::uint64_t{0});
  const json& w = field(style, "weights");
  for (int r = 0; r < kResourceCount; ++r) {
    bot.weights.resource[at(r)] = field(field(w, "resource"), std::string(resource_name(static_cast<Resource>(r))).c_str()).get<double>();
  }
  bot.weights.variety = field(w, "variety").get<double>();
  bot.weights.port = field(w, "port").get<double>();
  bot.weights.city = field(w, "city").get<double>();
  bot.weights.settlement = field(w, "settlement").get<double>();
  bot.weights.dev_card = field(w, "dev_card").get<double>();
  bot.weights.road_reach = field(w, "road_reach").get<int>();
  const json& t = field(style, "trading");
  bot.trading.offers_per_turn = field(t, "offers_per_turn").get<int>();
  bot.trading.accepts = field(t, "accepts").get<bool>();
  bot.trading.neutral = field(t, "neutral").get<bool>();
  bot.trading.embargo = field(t, "embargo").get<int>();
  return bot;
}

json board_json(const Board& b) {
  static constexpr std::array<const char*, 6> kTerrainNames = {"wood", "brick", "sheep", "wheat", "ore", "desert"};
  static constexpr std::array<const char*, 6> kPortNames = {"3:1",       "wood_2:1",  "brick_2:1",
                                                            "sheep_2:1", "wheat_2:1", "ore_2:1"};
  json hexes = json::array();
  for (int h = 0; h < kHexCount; ++h) {
    hexes.push_back({{"terrain", kTerrainNames[static_cast<std::size_t>(b.terrain[at(h)])]},
                     {"number", b.number[at(h)]}});
  }
  json ports = json::array();
  for (int p = 0; p < b.port_count; ++p) {
    ports.push_back({{"type", kPortNames[static_cast<std::size_t>(b.ports[at(p)].type)]},
                     {"nodes", nodes_json(b.ports[at(p)].nodes)}});
  }
  return {{"hexes", hexes}, {"ports", ports}};
}

Board board_from_json(const json& j) {
  try {
    Board b;
    const json& hexes = field(j, "hexes");
    if (!hexes.is_array() || hexes.size() != kHexCount) throw RecordError("the board must have 19 hexes");
    for (int h = 0; h < kHexCount; ++h) {
      const auto terrain = parse_terrain(field(hexes[at(h)], "terrain").get<std::string>());
      if (!terrain) throw RecordError("unknown terrain");
      b.terrain[at(h)] = *terrain;
      b.number[at(h)] = static_cast<std::int8_t>(field(hexes[at(h)], "number").get<int>());
    }
    const json& ports = field(j, "ports");
    if (!ports.is_array() || ports.size() > kMaxPorts) throw RecordError("too many ports");
    for (const json& port : ports) {
      const auto type = parse_port(field(port, "type").get<std::string>());
      if (!type) throw RecordError("unknown port type");
      Port p{*type, 0};
      for (const json& n : field(port, "nodes")) p.nodes |= bit(n.get<int>());
      b.ports[at(b.port_count++)] = p;
    }
    b.index_numbers();
    return b;
  } catch (const json::exception& e) {
    throw RecordError(std::string("broken board: ") + e.what());
  }
}

namespace {

std::optional<Phase> parse_phase(const std::string& name) {
  for (int p = 0; p <= static_cast<int>(Phase::kMain); ++p) {
    if (name == phase_name(static_cast<Phase>(p))) return static_cast<Phase>(p);
  }
  return std::nullopt;
}

Cards cards_from(const json& j) {
  Cards c{};
  if (!j.is_array() || j.size() != kResourceCount) throw RecordError("cards are five numbers");
  for (int r = 0; r < kResourceCount; ++r) c[at(r)] = j[at(r)].get<int>();
  return c;
}

std::uint8_t seats_from(const json& j) {
  std::uint8_t mask = 0;
  for (const json& s : j) mask = static_cast<std::uint8_t>(mask | (1U << s.get<int>()));
  return mask;
}

int seat_or_none(const json& j) { return j.is_null() ? -1 : j.get<int>(); }

Game from_snapshot(const json& board_j, const json& st) {
  const Board board = board_from_json(board_j);
  const json& players = field(st, "players");
  const int n = static_cast<int>(players.size());
  if (n < 2 || n > kMaxPlayers) throw RecordError("a game has 2 to 4 players");
  std::vector<DevCard> deck;
  for (const json& card : field(st, "dev_deck")) {
    const auto c = parse_dev_card(card.get<std::string>());
    if (!c) throw RecordError("unknown development card");
    deck.push_back(*c);
  }
  Game g = Game::create(board, Settings{}, n, 0, deck);
  for (int s = 0; s < n; ++s) {
    const json& pj = players[at(s)];
    Player& p = g.players[at(s)];
    p.resources = cards_from(field(pj, "resources"));
    for (int c = 0; c < kDevCardKinds; ++c) {
      p.dev_cards[at(c)] = static_cast<std::uint8_t>(field(pj, "dev_cards")[at(c)].get<int>());
      p.new_dev_cards[at(c)] = static_cast<std::uint8_t>(field(pj, "new_dev_cards")[at(c)].get<int>());
    }
    p.knights = field(pj, "knights").get<int>();
    for (const json& v : field(pj, "settlements")) p.settlements |= bit(v.get<int>());
    for (const json& v : field(pj, "cities")) p.cities |= bit(v.get<int>());
    for (const json& e : field(pj, "roads")) p.roads.set(e.get<int>());
  }
  g.current = field(st, "current").get<int>();
  g.turn_number = field(st, "turn").get<int>();
  const auto phase = parse_phase(field(st, "phase").get<std::string>());
  const auto back = parse_phase(field(st, "return_phase").get<std::string>());
  if (!phase || !back) throw RecordError("unknown phase");
  g.phase = *phase;
  g.return_phase = *back;
  g.dice_roll = field(st, "dice_roll").is_null() ? 0 : field(st, "dice_roll").get<int>();
  const json& counts = field(st, "roll_counts");
  for (std::size_t i = 0; i < g.roll_counts.size(); ++i) g.roll_counts[i] = counts.at(i).get<int>();
  // The snapshot lists the seats still to place; the queue starts over from them.
  const json& queue = field(st, "setup_queue");
  g.setup_length = static_cast<int>(queue.size());
  g.setup_position = 0;
  for (int i = 0; i < g.setup_length; ++i) g.setup_queue[at(i)] = static_cast<std::int8_t>(queue[at(i)].get<int>());
  g.setup_waiting_for_road = field(st, "setup_waiting_for_road").get<bool>();
  g.setup_last_node = seat_or_none(field(st, "setup_last_node"));
  g.robber_hex = field(st, "robber").get<int>();
  const json& pending = field(st, "pending_discards");
  for (int s = 0; s < n; ++s) g.pending_discards[at(s)] = static_cast<std::int8_t>(seat_or_none(pending[at(s)]));
  g.free_roads_left = field(st, "free_roads_left").get<int>();
  const json& offer = field(st, "offer");
  if (!offer.is_null()) {
    g.offer.open = true;
    g.offer.proposer = static_cast<std::int8_t>(field(offer, "proposer").get<int>());
    g.offer.give = cards_from(field(offer, "give"));
    g.offer.get = cards_from(field(offer, "get"));
    g.offer.accepted = seats_from(field(offer, "accepted"));
    g.offer.declined = seats_from(field(offer, "declined"));
  }
  g.dev_card_played = field(st, "dev_card_played").get<bool>();
  g.largest_army = seat_or_none(field(st, "largest_army"));
  g.longest_road = seat_or_none(field(st, "longest_road"));
  const json& lengths = field(st, "road_lengths");
  for (int s = 0; s < n; ++s) g.road_lengths[at(s)] = lengths[at(s)].get<int>();
  g.game_over = field(st, "game_over").get<bool>();
  g.winner = seat_or_none(field(st, "winner"));
  return g;
}

}  // namespace

Game game_from_snapshot(const json& board, const json& state) {
  try {
    return from_snapshot(board, state);
  } catch (const json::exception& e) {
    throw RecordError(std::string("broken snapshot: ") + e.what());
  }
}

json action_json(const Action& a) {
  json out = {{"kind", action_name(a.kind)}};
  switch (a.kind) {
    case ActionKind::kBuildSettlement:
    case ActionKind::kBuildRoad:
    case ActionKind::kUpgradeCity:
    case ActionKind::kMoveRobber:
    case ActionKind::kSteal:
    case ActionKind::kConfirmTrade: out["target"] = a.target; break;
    case ActionKind::kDiscard: out["give"] = cards_json(a.give); break;
    case ActionKind::kOfferTrade:
      out["give"] = cards_json(a.give);
      out["get"] = cards_json(a.get);
      break;
    case ActionKind::kRespondTrade: out["accept"] = a.accept; break;
    case ActionKind::kTradeWithBank:
    case ActionKind::kPlayYearOfPlenty:
      out["first"] = resource_name(a.first);
      out["second"] = resource_name(a.second);
      break;
    case ActionKind::kPlayMonopoly: out["first"] = resource_name(a.first); break;
    default: break;
  }
  return out;
}

std::optional<Action> action_from_json(const json& j) {
  try {
    if (!j.is_object() || !j.contains("kind") || !j["kind"].is_string()) return std::nullopt;
    Action a;
    bool found = false;
    for (int k = 0; k <= static_cast<int>(ActionKind::kEndTurn); ++k) {
      if (j["kind"] == action_name(static_cast<ActionKind>(k))) {
        a.kind = static_cast<ActionKind>(k);
        found = true;
      }
    }
    if (!found) return std::nullopt;
    a.target = j.value("target", -1);
    if (j.contains("give")) a.give = cards_from(j["give"]);
    if (j.contains("get")) a.get = cards_from(j["get"]);
    a.accept = j.value("accept", false);
    for (const auto& [key, out] : {std::pair<const char*, Resource*>{"first", &a.first}, {"second", &a.second}}) {
      if (!j.contains(key)) continue;
      const auto r = parse_resource(j[key]);
      if (!r) return std::nullopt;
      *out = *r;
    }
    return a;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

json read_record_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw RecordError("cannot open " + path);
  try {
    return json::parse(in);
  } catch (const json::parse_error& e) {
    throw RecordError(path + " is not JSON: " + e.what());
  }
}

}  // namespace cae
