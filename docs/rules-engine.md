# Rules engine

The rules of [catan-project](https://github.com/Qasdfyk/catan-project) (`backend/app/models/game.py`) in C++20, for replaying recorded games and, later, for simulating millions of them. It plays exactly the platform's rules, house options included (victory points, discard limit, friendly robber); other rule sets come later as options.

Code: `include/catan/topology.hpp` (the board's geometry), `include/catan/game.hpp` and `src/game.cpp` (state and rules), `include/catan/record.hpp` and `src/record.cpp` (game records). How it is checked against the Python rules: [difftest.md](difftest.md).

## Board: indices instead of coordinates

catan-project names positions by cube coordinates: a hex is `(q, r, s)` with `q + r + s = 0`, and a corner or a side is a hex plus a direction 0..5 (see "Positions" in its `docs/game-record.md`). That is convenient for drawing, but a corner has up to three spellings, and every rule check in Python goes through a canonicalising lookup and a hash.

The engine numbers everything once:

| What | Count | Index |
|---|---|---|
| hexes | 19 | in the order `Board._generate_hex_grid` produces them: q ascending, then r. The center is hex 9. |
| corners ("nodes") | 54 | in order of first sight while walking hex 0 directions 0..5, hex 1, and so on |
| sides ("edges") | 72 | the same way |

`node_at(q, r, d)` and `edge_at(q, r, d)` accept every spelling of a position, land or sea, and give -1 for anything off the board. Everything else about the geometry is a table in `Topology`: the six corners of each hex, the hexes, sides and neighbors of each corner, the two ends of each side.

The tables are computed by the compiler. `kTopology` is a `constexpr` variable whose value comes from running `topology_detail::build()` at compile time, so at run time they are constants in the binary, and the `static_assert`s at the end of the header fail the build if the numbering ever changes.

## State: bitmasks

A set of corners is a `NodeMask`: 54 corners, one bit each, in one 64-bit word (B4, `include/catan/bits.hpp`). A set of sides is an `EdgeMask`: 72 bits do not fit one word, so it is two, `lo` for sides 0..63 and `hi` for 64..71.

Each player has `settlements`, `cities` (`NodeMask`) and `roads` (`EdgeMask`). Then most rules are a few word operations:

| Question | Expression |
|---|---|
| How many of my buildings touch hex h? | `popcount(settlements & hex_node_mask[h]) + 2 * popcount(cities & ...)` |
| Is corner n free and not next to a building? | `!(occupied & (bit(n) \| node_neighbor_mask[n]))` |
| All legal settlement spots | `kAllNodes & ~(occupied \| neighbors of occupied) & corners my roads reach` |
| Does side e connect to my roads at corner n? | `(roads & node_edge_mask[n]).any()` |

Production after a roll looks only at `hexes_by_number[roll]`, a 19-bit mask of the hexes carrying that number, and loops over its set bits.

The whole `Game` is a flat struct of arrays and masks: no pointers, no `std::vector`, no heap. `sizeof(Game)` is 768 bytes, and copying a game is one `memcpy`. A search copies the position before each move it tries, so that matters later.

## Chance is an argument

`roll_dice(seat, die1, die2)` takes the dice; `steal(seat, victim, card)` takes the card. The engine never draws a random number, and the development deck is fixed when the game is created. Replaying a record passes the recorded outcomes; a simulation will pass random ones. The engine is therefore deterministic: the same inputs give the same game, which is what makes the difference test and, later, reproducible simulations possible.

## Actions and refusals

Each client action of catan-project is one member function returning an `Error`. `Error::kNone` means the state changed; anything else means it is exactly as before (the `Refusals` test checks this by comparing snapshots). The checks run in the order `game.py` runs them, so both engines refuse the same actions; the reason may differ when several apply, and the difference test compares only accepted against refused.

Points are not stored. `visible_points(seat)` counts settlements, cities and the two awards from the masks each time, so they cannot drift from the board. `hidden_points` counts victory point cards in hand.

The longest road is a depth-first search over the player's roads with a mask of sides already used, as in `game.py`: a trail may start at an opponent's building but not pass through one. It is exponential in the worst case and fast enough for replays; making it fast is a job for step B8.

## Records

`catan_record` is a separate library on top of the rules. It reads the JSON header (`game_from_record`), applies events (`apply_event`) and writes the state as JSON (`snapshot`, `legal_moves`). The rules library has no dependencies, so it can be compiled to WebAssembly as it is; JSON comes in only with records.

`apply_event` parses payloads as strictly as `app/services/actions.py` does (ints that are not bools, cube coordinates that sum to zero, directions 0..5, cards up to 200) and turns anything malformed into `Error::kBadArgument`, never a crash. Recorded chance that cannot happen (a die of 7, a stolen card the victim did not hold, a drawn card that is not the next one in the deck) is `Error::kBadOutcome`.

## Tools

```sh
build/release/tools/catan_replay game.json
```

prints the board, each player's expected cards per 36 rolls after the setup, the result, and how long the replay took. It exits with 1 if an event is refused or the result differs from the one in the record.
