"""
Differential test: the C++ rules against catan-project's game.py.

Both engines run side by side in one Python process (the C++ one through the
cae_py module, see python/bindings.cpp). After every single action the test
compares the whole state and the legal moves of every seat. Any difference
stops the run and prints the first field that differs.

Three parts:
  topology  every corner and side of the Python board maps to exactly one
            engine index, with the same neighbors
  records   recorded games (tests/data, catan-project's backend/records and
            any --records) replay event by event in both engines
  random    random games: the random player of catan-project, plus "probes",
            random actions with random arguments that are mostly illegal; both
            engines must accept or refuse each one alike. Every other game
            also fills hands so the bank runs short, which random play alone
            almost never does.

Run with the catan-project backend's Python, after building the module:
  cmake --preset python && cmake --build --preset python
  ../catan-project/backend/.venv/bin/python tools/difftest.py --games 20
"""
import argparse
import glob
import gzip
import json
import os
import random
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RESOURCES = ["wood", "brick", "sheep", "wheat", "ore"]
DEV_CARDS = ["knight", "victory_point", "road_building", "year_of_plenty", "monopoly"]
ACTIONS = [
    "build_settlement", "build_road", "upgrade_city", "roll_dice", "discard", "move_robber",
    "trade_with_bank", "offer_trade", "respond_trade", "confirm_trade", "cancel_trade",
    "buy_dev_card", "play_knight", "play_road_building", "play_year_of_plenty", "play_monopoly",
    "end_turn", "steal",
]


class Difference(Exception):
    pass


def first_difference(a, b, path="$"):
    """The path of the first place where two JSON-like values differ, or None."""
    if isinstance(a, dict) and isinstance(b, dict):
        for key in sorted(set(a) | set(b)):
            if key not in a or key not in b:
                return f"{path}.{key} (missing on one side)"
            found = first_difference(a[key], b[key], f"{path}.{key}")
            if found:
                return found
        return None
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return f"{path} (length {len(a)} vs {len(b)}): {a} vs {b}"
        for i, (x, y) in enumerate(zip(a, b)):
            found = first_difference(x, y, f"{path}[{i}]")
            if found:
                return found
        return None
    if a != b or type(a) is not type(b):
        return f"{path}: python {a!r} vs c++ {b!r}"
    return None


class Bridge:
    """Translates the Python game into the engine's JSON form."""

    def __init__(self, cae):
        self.cae = cae

    def node(self, vertex):
        v = vertex.get_canonical()
        return self.cae.node_at(v.owner.q, v.owner.r, v.direction)

    def edge(self, edge):
        e = edge.get_canonical()
        return self.cae.edge_at(e.owner.q, e.owner.r, e.direction)

    def hex(self, h):
        return self.cae.hex_at(h.q, h.r)

    def snapshot(self, game):
        from app.models.game import BuildingType
        from app.models.player import DevCard
        from app.models.board import ResourceType

        seat = {p.color: i for i, p in enumerate(game.players)}

        def cards(d):
            return [d.get(ResourceType(r), 0) for r in RESOURCES]

        players = []
        for p in game.players:
            mine = [(v, b) for v, b in game.settlements.items() if b.owner == p.color]
            players.append({
                "resources": [p.resources[ResourceType(r)] for r in RESOURCES],
                "points": p.victory_points,
                "hidden_points": p.hidden_points(),
                "dev_cards": [p.dev_cards.count(DevCard(c)) for c in DEV_CARDS],
                "new_dev_cards": [p.new_dev_cards.count(DevCard(c)) for c in DEV_CARDS],
                "knights": p.knights_played,
                "settlements": sorted(self.node(v) for v, b in mine if b.type == BuildingType.SETTLEMENT),
                "cities": sorted(self.node(v) for v, b in mine if b.type == BuildingType.CITY),
                "roads": sorted(self.edge(e) for e, c in game.roads.items() if c == p.color),
            })
        offer = None
        if game.trade_offer is not None:
            o = game.trade_offer
            offer = {
                "proposer": seat[o.proposer],
                "give": cards(o.give),
                "get": cards(o.get),
                "accepted": sorted(seat[c] for c in o.accepted),
                "declined": sorted(seat[c] for c in o.declined),
            }
        return {
            "players": players,
            "current": game.current_turn_index,
            "turn": game.turn_number,
            "phase": game.turn_phase.value,
            "dice_roll": game.dice_roll,
            "roll_counts": list(game.roll_counts),
            "setup_queue": list(game.setup_queue),
            "setup_waiting_for_road": game.setup_waiting_for_road,
            "setup_last_node": self.node(game.setup_last_vertex) if game.setup_last_vertex else None,
            "robber": self.hex(game.robber_hex),
            "pending_discards": [game.pending_discards.get(p.color) for p in game.players],
            "return_phase": game.return_phase.value,
            "free_roads_left": game.free_roads_left,
            "offer": offer,
            # The game draws from the end of its list; the engine keeps draw order.
            "dev_deck": [c.value for c in reversed(game.dev_deck)],
            "dev_card_played": game.dev_card_played,
            "largest_army": seat.get(game.largest_army_holder),
            "longest_road": seat.get(game.longest_road_holder),
            "road_lengths": [game.road_lengths.get(p.color, 0) for p in game.players],
            "game_over": game.is_game_over,
            "winner": seat[game.winner.color] if game.winner else None,
        }

    def legal(self, game, s):
        from app.models.game import TurnPhase

        p = game.players[s]
        robbing = not game.is_game_over and game.current_turn_index == s and game.turn_phase == TurnPhase.STEAL
        return {
            "settlements": sorted(self.node(v) for v in game.legal_settlements(p)),
            "roads": sorted(self.edge(e) for e in game.legal_roads(p)),
            "cities": sorted(self.node(v) for v in game.legal_cities(p)),
            "robber_hexes": sorted(self.hex(h) for h in game.legal_robber_hexes(p)),
            "steal_victims": sorted(game.seat_of(v) for v in game.steal_victims(p)) if robbing else [],
            "playable_dev_cards": sorted((c.value for c in game.playable_dev_cards(p)), key=DEV_CARDS.index),
            "can_buy_dev_card": game.can_buy_dev_card(p),
        }

    def compare(self, game, engine, where, all_seats=True):
        found = first_difference(self.snapshot(game), json.loads(engine.snapshot()))
        if found:
            raise Difference(f"{where}: state differs at {found}")
        seats = range(len(game.players)) if all_seats else [game.current_turn_index]
        for s in seats:
            found = first_difference(self.legal(game, s), json.loads(engine.legal(s)))
            if found:
                raise Difference(f"{where}: legal moves of seat {s} differ at {found}")


def check_topology(cae, bridge):
    from app.models.board import Board
    from app.models.hex_lib import Edge, Hex, Vertex

    board = Board.create_standard_game()
    nodes = {bridge.node(v) for v in board.vertices()}
    edges = {bridge.edge(e) for e in board.edges()}
    if nodes != set(range(54)) or edges != set(range(72)):
        raise Difference(f"topology: {len(nodes)} corners and {len(edges)} sides map to {sorted(nodes)[:5]}...")
    for v in board.vertices():
        theirs = {bridge.node(n) for n in v.get_adjacent_vertices() if board.is_on_board(n)}
        if theirs != set(cae.node_neighbors(bridge.node(v))):
            raise Difference(f"topology: neighbors of {v} differ")
    for e in board.edges():
        if {bridge.node(v) for v in e.get_vertices()} != set(cae.edge_nodes(bridge.edge(e))):
            raise Difference(f"topology: ends of {e} differ")
    # Every spelling, land or sea, of every corner and side within 3 rings.
    spellings = 0
    for q in range(-3, 4):
        for r in range(-3, 4):
            if max(abs(q), abs(r), abs(q + r)) > 3:
                continue
            h = Hex(q, r, -q - r)
            for d in range(6):
                v, e = Vertex(h, d), Edge(h, d)
                want_v = bridge.node(v) if board.is_on_board(v) else -1
                want_e = bridge.edge(e) if board.is_on_board(e) else -1
                if cae.node_at(q, r, d) != want_v or cae.edge_at(q, r, d) != want_e:
                    raise Difference(f"topology: spelling {h} direction {d} maps differently")
                spellings += 1
    print(f"topology: 54 corners, 72 sides, {spellings} spellings agree")


def load(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        return json.load(f)


def check_records(cae, bridge, paths):
    from app.services.actions import apply_action
    from app.services.game_record import _RecordedChance, _start

    events = 0
    for path in paths:
        record = load(path)
        game = _start(record)
        engine = cae.Engine(json.dumps(record))
        name = os.path.basename(path)
        bridge.compare(game, engine, f"{name} start")
        for i, event in enumerate(record["events"]):
            game.rng = _RecordedChance(i, event.get("outcome", {}))
            apply_action(game, event["seat"], event["action"], event["payload"])
            error = engine.apply(json.dumps(event))
            if error:
                raise Difference(f"{name} event {i} ({event['action']}): python accepted, c++ refused: {error}")
            bridge.compare(game, engine, f"{name} event {i} ({event['action']})")
        events += len(record["events"])
    print(f"records: {len(paths)} games, {events} events, no differences")


def random_cube(rng, radius=3):
    while True:
        q, r = rng.randint(-radius, radius), rng.randint(-radius, radius)
        if abs(q + r) <= radius:
            return {"q": q, "r": r, "s": -q - r}


def random_cards(rng):
    names = RESOURCES + ["desert"]
    return {rng.choice(names): rng.choice([-1, 0, 1, 1, 2, 3, 4]) for _ in range(rng.randint(0, 3))}


def random_probe(game, rng):
    """
    A random action with random arguments, most of them illegal. Half the
    arguments are aimed near the legal ones (the robber's own hex, a corner
    that already has a building, the seat that has to discard), because a
    purely random corner rarely reaches the checks that matter.
    """
    from app.models.game import TurnPhase

    n = len(game.players)
    seat = game.current_turn_index if rng.random() < 0.7 else rng.randrange(n)
    if game.turn_phase == TurnPhase.DISCARD and game.pending_discards and rng.random() < 0.5:
        color = rng.choice(list(game.pending_discards))
        seat = next(i for i, p in enumerate(game.players) if p.color == color)
    action = rng.choice(ACTIONS)
    aimed = rng.random() < 0.5

    position = {"hex": random_cube(rng), "direction": rng.randrange(6)}
    if aimed and game.settlements and action in ("build_settlement", "upgrade_city", "build_road"):
        v = rng.choice(list(game.settlements))
        position = {"hex": {"q": v.owner.q, "r": v.owner.r, "s": v.owner.s}, "direction": v.direction}
    robber_hex = random_cube(rng)
    if aimed and game.robber_hex is not None:
        h = game.robber_hex
        robber_hex = {"q": h.q, "r": h.r, "s": h.s}
    cards = random_cards(rng)
    if aimed and action == "discard":
        hand = [r for r, k in game.players[seat].resources.items() for _ in range(k)]
        want = game.pending_discards.get(game.players[seat].color, 0) + rng.choice([-1, 0, 0, 1])
        picked = rng.sample(hand, max(0, min(want, len(hand))))
        cards = {r.value: picked.count(r) for r in set(picked)}
    resource = rng.choice(RESOURCES + ["desert"])
    payload = {
        "build_settlement": position,
        "build_road": position,
        "upgrade_city": position,
        "discard": {"cards": cards},
        "move_robber": {"hex": robber_hex},
        "trade_with_bank": {"give": resource, "get": rng.choice(RESOURCES)},
        "offer_trade": {"give": random_cards(rng), "get": random_cards(rng)},
        "respond_trade": {"accept": rng.random() < 0.6},
        "confirm_trade": {"partner_seat": rng.randrange(n + 1)},
        "play_year_of_plenty": {"first": resource, "second": rng.choice(RESOURCES)},
        "play_monopoly": {"resource": resource},
        "steal": {"victim_seat": rng.randrange(n + 1)},
    }.get(action, {})
    return seat, action, payload


def squeeze_bank(game, engine, rng):
    """
    Hands one player a pile of one resource, in both engines alike, so the
    bank runs short. Random play almost never empties the bank by itself, and
    the rule for a short bank (nobody gets that resource when several players
    should) went untested until this was added.
    """
    from app.models.board import ResourceType

    seat = rng.randrange(len(game.players))
    player = game.players[seat]
    resource = rng.choice(RESOURCES)
    held_by_others = sum(p.resources[ResourceType(resource)] for p in game.players if p is not player)
    # Leave 1 to 3 cards in the bank: with 0 left, "nobody gets any" and
    # "everyone gets what is left" look the same, so 0 tests nothing.
    player.resources[ResourceType(resource)] = max(0, 19 - held_by_others - rng.randint(1, 3))
    engine.set_hand(seat, [player.resources[ResourceType(r)] for r in RESOURCES])


def check_random(cae, bridge, games, seed, probe_rate, max_steps):
    from app.models.game import GameState
    from app.models.settings import GameSettings
    from app.services.actions import apply_action
    from app.services.game_record import new_record
    from app.services.random_player import play_step

    steps = probes = accepted = 0
    for g in range(games):
        game_seed = seed + g
        rng = random.Random(game_seed)
        random.seed(game_seed)  # the board and the deck come from the global generator
        players = rng.choice([2, 3, 4, 4])
        settings = GameSettings(
            victory_points=rng.choice([10, 10, 8]),
            discard_limit=rng.choice([7, 7, 9]),
            friendly_robber=rng.random() < 0.5,
        )
        game = GameState.create_new_game(["A", "B", "C", "D"][:players], settings, first_seat=rng.randrange(players))
        game.rng = random.Random(game_seed * 7919)  # dice and steals
        engine = cae.Engine(json.dumps(new_record(game, source="simulation")))
        where = f"random game {g} (seed {game_seed})"
        bridge.compare(game, engine, f"{where} start")
        # Every other game squeezes the bank from time to time.
        squeeze = g % 2 == 1

        for step in range(max_steps):
            if game.is_game_over:
                break
            if squeeze and step > 4 * players and rng.random() < 0.05:
                squeeze_bank(game, engine, rng)
            if rng.random() < probe_rate:
                seat, action, payload = random_probe(game, rng)
                probes += 1
                try:
                    apply_action(game, seat, action, payload)
                    accepted += 1
                except ValueError:
                    # A refused action records no outcome. The engine needs one
                    # to roll, and without one refuses a steal it would allow
                    # with "bad_outcome", which here means it disagrees.
                    event = {"seat": seat, "action": action, "payload": payload}
                    if action == "roll_dice":
                        event["outcome"] = {"dice": [3, 4]}
                    error = engine.apply(json.dumps(event))
                    if error in ("", "bad_outcome"):
                        raise Difference(f"{where} step {step}: python refused {event}, c++ would accept it")
            else:
                play_step(game, rng)
            for event in game.take_events():
                error = engine.apply(json.dumps(event))
                if error:
                    raise Difference(f"{where} step {step}: python accepted {event}, c++ refused: {error}")
            # Legal moves of every seat now and then; of the seat to move always.
            bridge.compare(game, engine, f"{where} step {step}", all_seats=step % 8 == 0)
            steps += 1

        # After the end every action must be refused by both.
        for _ in range(20 if game.is_game_over else 0):
            seat, action, payload = random_probe(game, rng)
            probes += 1
            try:
                apply_action(game, seat, action, payload)
                raise Difference(f"{where}: python accepted {action} after the game ended")
            except ValueError:
                pass
            event = {"seat": seat, "action": action, "payload": payload, "outcome": {"dice": [3, 4]}}
            if engine.apply(json.dumps(event)) in ("", "bad_outcome"):
                raise Difference(f"{where}: c++ would accept {action} after the game ended")
    print(f"random: {games} games, {steps} steps, {probes} probes ({accepted} accepted), no differences")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=os.path.join(ROOT, "build", "python", "python"),
                        help="directory holding the cae_py module")
    parser.add_argument("--backend", default=os.path.join(ROOT, "..", "catan-project", "backend"),
                        help="catan-project's backend directory")
    parser.add_argument("--records", nargs="*", default=[], help="more record files (.json or .json.gz)")
    parser.add_argument("--games", type=int, default=20, help="random games to play")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--probe-rate", type=float, default=0.3, help="share of steps that are random probes")
    parser.add_argument("--max-steps", type=int, default=20_000, help="cap on steps per random game")
    args = parser.parse_args(argv)

    sys.path.insert(0, os.path.abspath(args.build))
    sys.path.insert(0, os.path.abspath(args.backend))
    import cae_py

    bridge = Bridge(cae_py)
    records = sorted(glob.glob(os.path.join(ROOT, "tests", "data", "*.json")))
    records += sorted(glob.glob(os.path.join(os.path.abspath(args.backend), "records", "*.json.gz")))
    records += args.records

    started = time.perf_counter()
    try:
        check_topology(cae_py, bridge)
        check_records(cae_py, bridge, records)
        check_random(cae_py, bridge, args.games, args.seed, args.probe_rate, args.max_steps)
    except Difference as difference:
        print(f"DIFFERENCE: {difference}")
        return 1
    print(f"difftest passed in {time.perf_counter() - started:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
