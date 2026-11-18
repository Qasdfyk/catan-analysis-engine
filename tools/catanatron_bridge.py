"""
Catanatron as a player in catan_arena (tools/arena.cpp).

Catanatron (github.com/bcollazo/catanatron, GPL-3.0) is the strongest public
Catan bot we know of. It is not part of this repository and none of its code
is copied here: this script imports it from wherever it is installed and runs
as a separate process. catan_arena starts it and speaks JSON lines with it
over stdin and stdout:

  -> {"type": "hello", "topology": {...}}         <- {"ok": true}
  -> {"type": "decide", "seat", "board", "state", "built"}
                                                  <- {"action": {...}, "victim": 2}

Our engine is the referee. For every decision this script builds a
Catanatron state from our position (board, pieces in the order they were
built, hands, development cards, bank, phase), asks Catanatron's player for
a move and translates it back. Nothing is kept between decisions, so the two
engines cannot drift apart.

Translation:
  - Hexes: our axial (q, r) is Catanatron's cube (q, -q-r, r); corner d of
    our hex is Catanatron's node ref NE, SE, S, SW, NW, N for d = 0..5.
    hello checks that every hex, corner and side maps one to one.
  - Ports sit where our board has them: each becomes a Catanatron port tile
    on the sea hex and side that touch the same two corners.
  - A discard is many decisions in Catanatron (one card each) and one move
    in ours; the script asks card by card and sends the total.
  - Catanatron moves the robber and names the victim in one move; ours takes
    two. The victim comes back with the robber move and catan_arena plays it.
  - Catanatron has no trading between players; catan_arena declines offers
    for it without asking.

Catanatron's bots see every hand (its state has no hidden information), so
they play with more knowledge than ours. docs/arena.md says so next to
the results.

Install (the PyPI package 3.2.1 lacks AlphaBetaPlayer; the GitHub source has it):
  git clone https://github.com/bcollazo/catanatron /tmp/catanatron-src
  git -C /tmp/catanatron-src checkout ecf9311
  uv venv /tmp/catanatron && VIRTUAL_ENV=/tmp/catanatron uv pip install /tmp/catanatron-src
Then pass --python /tmp/catanatron/bin/python to catan_arena.

Usage: python catanatron_bridge.py alphabeta|weighted
"""

import json
import os
import sys
from collections import defaultdict

from catanatron.game import Game
from catanatron.models.board import Board
from catanatron.models.coordinate_system import Direction
from catanatron.models.enums import (
    BRICK,
    CITY,
    ORE,
    ROAD,
    SETTLEMENT,
    SHEEP,
    WHEAT,
    WOOD,
    ActionPrompt,
    ActionType,
    NodeRef,
)
from catanatron.models.map import (
    BASE_MAP_TEMPLATE,
    PORT_DIRECTION_TO_NODEREFS,
    CatanMap,
    initialize_tiles,
)
from catanatron.models.player import Color, SimplePlayer
from catanatron.models.tiles import LandTile, Port, Water
from catanatron.state import PLAYER_INITIAL_STATE

RESOURCES = [WOOD, BRICK, SHEEP, WHEAT, ORE]
OUR_RESOURCES = ["wood", "brick", "sheep", "wheat", "ore"]
TERRAIN = {"wood": WOOD, "brick": BRICK, "sheep": SHEEP, "wheat": WHEAT, "ore": ORE, "desert": None}
PORT_TYPES = {"3:1": None, "wood_2:1": WOOD, "brick_2:1": BRICK, "sheep_2:1": SHEEP,
              "wheat_2:1": WHEAT, "ore_2:1": ORE}
# Our DevCard order: knight, victory point, road building, year of plenty, monopoly
DEV_CARDS = ["KNIGHT", "VICTORY_POINT", "ROAD_BUILDING", "YEAR_OF_PLENTY", "MONOPOLY"]
CORNER_REFS = [NodeRef.NORTHEAST, NodeRef.SOUTHEAST, NodeRef.SOUTH,
               NodeRef.SOUTHWEST, NodeRef.NORTHWEST, NodeRef.NORTH]
COLORS = [Color.RED, Color.BLUE, Color.ORANGE, Color.WHITE]


def cube(q, r):
    return (q, -q - r, r)


class Mapping:
    """Our indices against Catanatron's ids, from the hello topology."""

    def __init__(self, topology):
        self.hexes = [cube(h["q"], h["r"]) for h in topology["hexes"]]
        self.template = initialize_tiles(BASE_MAP_TEMPLATE)
        land = {c: t for c, t in self.template.items() if isinstance(t, LandTile)}
        if set(land) != set(self.hexes):
            raise ValueError("the land hexes differ")
        self.node = {}  # ours -> theirs
        for h, corners in enumerate(topology["hex_nodes"]):
            tile = land[self.hexes[h]]
            for d, ours in enumerate(corners):
                theirs = tile.nodes[CORNER_REFS[d]]
                if self.node.setdefault(ours, theirs) != theirs:
                    raise ValueError(f"corner {ours} maps to two nodes")
        if sorted(self.node.values()) != list(range(54)):
            raise ValueError("corners do not map one to one")
        self.node_back = {v: k for k, v in self.node.items()}
        self.edge = {}
        self.edge_back = {}
        land_edges = {tuple(sorted(e)) for t in land.values() for e in t.edges.values()}
        for e, side in enumerate(topology["edges"]):
            a, b = (self.node[n] for n in side["nodes"])
            if tuple(sorted((a, b))) not in land_edges:
                raise ValueError(f"side {e} is not a side in Catanatron")
            self.edge[e] = (a, b)
            self.edge_back[(a, b)] = e
            self.edge_back[(b, a)] = e
        if len(self.edge) != 72:
            raise ValueError("sides do not map one to one")

    def catan_map(self, board):
        """A Catanatron map with our terrain, numbers and ports. The board of a
        game never changes, so the last one is kept."""
        key = json.dumps(board, sort_keys=True)
        if getattr(self, "_map_key", None) != key:
            self._map = self._build_map(board)
            self._map_key = key
        return self._map

    def _build_map(self, board):
        tiles = {}
        next_id = 0
        for coordinate, tile in self.template.items():
            if isinstance(tile, LandTile):
                h = self.hexes.index(coordinate)
                ours = board["hexes"][h]
                resource = TERRAIN[ours["terrain"]]
                tiles[coordinate] = LandTile(next_id, resource, ours["number"] or None,
                                             tile.nodes, tile.edges)
                next_id += 1
            else:  # sea; ports are placed below
                tiles[coordinate] = Water(tile.nodes, tile.edges)
        for port in board["ports"]:
            wanted = {self.node[n] for n in port["nodes"]}
            placed = False
            for coordinate, tile in list(tiles.items()):
                if not isinstance(tile, Water):
                    continue
                for direction in Direction:
                    a, b = PORT_DIRECTION_TO_NODEREFS[direction]
                    if {tile.nodes[a], tile.nodes[b]} == wanted:
                        tiles[coordinate] = Port(next_id, PORT_TYPES[port["type"]], direction,
                                                 tile.nodes, tile.edges)
                        next_id += 1
                        placed = True
                        break
                if placed:
                    break
            if not placed:
                raise ValueError(f"no sea side for the port at {port['nodes']}")
        return CatanMap.from_tiles(tiles)


def build_game(mapping, msg, player):
    """A Catanatron Game in the position of our snapshot."""
    st = msg["state"]
    n = len(st["players"])
    colors = COLORS[:n]
    players = [player if s == msg["seat"] else SimplePlayer(colors[s]) for s in range(n)]
    for s, p in enumerate(players):
        p.color = colors[s]
    game = Game(players, seed=0, catan_map=mapping.catan_map(msg["board"]))
    state = game.state
    # Seats in our order (Game shuffles them)
    state.players = players
    state.colors = tuple(colors)
    state.color_to_index = {c: i for i, c in enumerate(colors)}
    state.buildings_by_color = {c: defaultdict(list) for c in colors}

    # Pieces in the order they were built, so Catanatron's road networks and
    # "last settlement" (for the setup road) come out as in the game.
    board = Board(state.board.map)
    setup = st["phase"] == "setup"
    settlements = [0] * n
    for kind, seat, where in msg["built"]:
        color = colors[seat]
        if kind == 0:
            node = mapping.node[where]
            # The first two of each player are the setup ones, placed anywhere;
            # later ones join the road network (and may cut an opponent's).
            board.build_settlement(color, node, initial_build_phase=settlements[seat] < 2)
            settlements[seat] += 1
            state.buildings_by_color[color][SETTLEMENT].append(node)
        elif kind == 1:
            node = mapping.node[where]
            board.build_city(color, node)
            state.buildings_by_color[color][SETTLEMENT].remove(node)
            state.buildings_by_color[color][CITY].append(node)
        else:
            edge = mapping.edge[where]
            board.build_road(color, edge)
            state.buildings_by_color[color][ROAD].append(edge)
    board.robber_coordinate = mapping.hexes[st["robber"]]
    state.board = board

    ps = {}
    for s, pj in enumerate(st["players"]):
        k = f"P{s}"
        for key, value in PLAYER_INITIAL_STATE.items():
            ps[f"{k}_{key}"] = value
        for r, name in enumerate(RESOURCES):
            ps[f"{k}_{name}_IN_HAND"] = pj["resources"][r]
        for c, name in enumerate(DEV_CARDS):
            ps[f"{k}_{name}_IN_HAND"] = pj["dev_cards"][c] + pj["new_dev_cards"][c]
            if name != "VICTORY_POINT":
                ps[f"{k}_{name}_OWNED_AT_START"] = pj["dev_cards"][c] > 0
        ps[f"{k}_PLAYED_KNIGHT"] = pj["knights"]
        ps[f"{k}_VICTORY_POINTS"] = pj["points"]
        ps[f"{k}_ACTUAL_VICTORY_POINTS"] = pj["points"] + pj["hidden_points"]
        ps[f"{k}_ROADS_AVAILABLE"] = 15 - len(pj["roads"])
        ps[f"{k}_SETTLEMENTS_AVAILABLE"] = 5 - len(pj["settlements"])
        ps[f"{k}_CITIES_AVAILABLE"] = 4 - len(pj["cities"])
        ps[f"{k}_HAS_ROAD"] = st["longest_road"] == s
        ps[f"{k}_HAS_ARMY"] = st["largest_army"] == s
        ps[f"{k}_LONGEST_ROAD_LENGTH"] = st["road_lengths"][s]
    cur = st["current"]
    ps[f"P{cur}_HAS_ROLLED"] = st["dice_roll"] is not None
    ps[f"P{cur}_HAS_PLAYED_DEVELOPMENT_CARD_IN_TURN"] = st["dev_card_played"]
    state.player_state = ps
    if st["longest_road"] is not None:
        board.road_color = colors[st["longest_road"]]
        board.road_length = st["road_lengths"][st["longest_road"]]

    bank = [19] * 5
    for pj in st["players"]:
        for r in range(5):
            bank[r] -= pj["resources"][r]
    state.resource_freqdeck = bank
    state.development_listdeck = [card.upper() for card in st["dev_deck"]]
    state.num_turns = st["turn"]

    seat = msg["seat"]
    state.current_turn_index = cur
    state.current_player_index = seat
    state.is_initial_build_phase = setup
    state.is_discarding = False
    state.discard_counts = [0] * n
    state.is_moving_knight = False
    state.is_road_building = False
    state.free_roads_available = 0
    phase = st["phase"]
    if phase == "setup":
        state.current_prompt = (ActionPrompt.BUILD_INITIAL_ROAD if st["setup_waiting_for_road"]
                                else ActionPrompt.BUILD_INITIAL_SETTLEMENT)
    elif phase == "discard":
        state.current_prompt = ActionPrompt.DISCARD
        state.is_discarding = True
        state.discard_counts = [d or 0 for d in st["pending_discards"]]
    elif phase == "move_robber":
        state.current_prompt = ActionPrompt.MOVE_ROBBER
        state.is_moving_knight = True
    elif phase == "road_building":
        state.current_prompt = ActionPrompt.PLAY_TURN
        state.is_road_building = True
        state.free_roads_available = st["free_roads_left"]
    else:
        state.current_prompt = ActionPrompt.PLAY_TURN
    from catanatron.models.actions import generate_playable_actions
    game.playable_actions = generate_playable_actions(state)
    return game


def resource_index(name):
    return OUR_RESOURCES[RESOURCES.index(name)]


def translate(mapping, action):
    """A Catanatron action as our move (and the victim of a robber move)."""
    t, v = action.action_type, action.value
    if t == ActionType.ROLL:
        return {"kind": "roll_dice"}, None
    if t == ActionType.END_TURN:
        return {"kind": "end_turn"}, None
    if t == ActionType.BUILD_SETTLEMENT:
        return {"kind": "build_settlement", "target": mapping.node_back[v]}, None
    if t == ActionType.BUILD_CITY:
        return {"kind": "upgrade_city", "target": mapping.node_back[v]}, None
    if t == ActionType.BUILD_ROAD:
        return {"kind": "build_road", "target": mapping.edge_back[tuple(v)]}, None
    if t == ActionType.BUY_DEVELOPMENT_CARD:
        return {"kind": "buy_dev_card"}, None
    if t == ActionType.PLAY_KNIGHT_CARD:
        return {"kind": "play_knight"}, None
    if t == ActionType.PLAY_ROAD_BUILDING:
        return {"kind": "play_road_building"}, None
    if t == ActionType.PLAY_MONOPOLY:
        return {"kind": "play_monopoly", "first": resource_index(v)}, None
    if t == ActionType.PLAY_YEAR_OF_PLENTY:
        cards = list(v) * (2 if len(v) == 1 else 1)
        return {"kind": "play_year_of_plenty", "first": resource_index(cards[0]),
                "second": resource_index(cards[1])}, None
    if t == ActionType.MARITIME_TRADE:
        return {"kind": "trade_with_bank", "first": resource_index(v[0]),
                "second": resource_index(v[4])}, None
    if t == ActionType.MOVE_ROBBER:
        coordinate, victim = v[0], v[1]
        seat = None if victim is None else COLORS.index(victim)
        return {"kind": "move_robber", "target": mapping.hexes.index(coordinate)}, seat
    return None, None


def legal_differences(mapping, game, msg):
    """Where Catanatron's legal moves differ from ours (CATANATRON_BRIDGE_CHECK=1).
    Robber moves are left out: ours lists hexes, Catanatron hex and victim."""
    ours = msg["legal"]
    theirs = {"settlements": set(), "cities": set(), "roads": set(), "buy": False}
    for a in game.playable_actions:
        t = a.action_type
        if t == ActionType.BUILD_SETTLEMENT:
            theirs["settlements"].add(mapping.node_back[a.value])
        elif t == ActionType.BUILD_CITY:
            theirs["cities"].add(mapping.node_back[a.value])
        elif t == ActionType.BUILD_ROAD:
            theirs["roads"].add(mapping.edge_back[tuple(a.value)])
        elif t == ActionType.BUY_DEVELOPMENT_CARD:
            theirs["buy"] = True
    out = []
    for key in ("settlements", "cities", "roads"):
        if set(ours[key]) != theirs[key]:
            out.append(f"{key}: ours {sorted(ours[key])} theirs {sorted(theirs[key])}")
    if ours["can_buy_dev_card"] != theirs["buy"] and msg["state"]["phase"] == "main_phase":
        out.append(f"buy: ours {ours['can_buy_dev_card']} theirs {theirs['buy']}")
    return out


def decide(mapping, player, msg):
    game = build_game(mapping, msg, player)
    state = game.state
    if os.environ.get("CATANATRON_BRIDGE_CHECK") and "legal" in msg:
        for d in legal_differences(mapping, game, msg):
            print(f"catanatron_bridge: {msg['state']['phase']}: {d}", file=sys.stderr)
    if state.current_prompt == ActionPrompt.DISCARD:
        # One card per Catanatron decision; ours discards them all at once.
        seat = msg["seat"]
        give = [0] * 5
        for _ in range(state.discard_counts[seat]):
            action = player.decide(game, game.playable_actions)
            give[RESOURCES.index(action.value)] += 1
            game.execute(action)
            if state.current_player_index != seat:
                break
        return {"action": {"kind": "discard", "give": give}}
    action = player.decide(game, game.playable_actions)
    ours, victim = translate(mapping, action)
    reply = {"action": ours, "catanatron": str(action.action_type.value)}
    if victim is not None:
        reply["victim"] = victim
    return reply


def make_player(kind):
    if kind == "alphabeta":
        from catanatron.players.minimax import AlphaBetaPlayer
        return AlphaBetaPlayer(Color.RED)
    if kind == "weighted":
        from catanatron.players.weighted_random import WeightedRandomPlayer
        return WeightedRandomPlayer(Color.RED)
    raise SystemExit(f"unknown player {kind}")


def main():
    player = make_player(sys.argv[1] if len(sys.argv) > 1 else "alphabeta")
    mapping = None
    # CATANATRON_BRIDGE_LOG=path writes every message and reply there, for debugging.
    log = open(os.environ["CATANATRON_BRIDGE_LOG"], "a") if os.environ.get("CATANATRON_BRIDGE_LOG") else None
    for line in sys.stdin:
        msg = json.loads(line)
        if log:
            log.write(line)
        try:
            if msg["type"] == "hello":
                mapping = Mapping(msg["topology"])
                reply = {"ok": True}
            else:
                reply = decide(mapping, player, msg)
        except Exception as e:  # report and let the arena play its safe move
            print(f"catanatron_bridge: {type(e).__name__}: {e}", file=sys.stderr)
            reply = {"ok": False, "error": str(e)}
        if log:
            log.write(json.dumps(reply) + "\n")
            log.flush()
        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
