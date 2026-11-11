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
