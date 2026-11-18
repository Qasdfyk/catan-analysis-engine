# The bot zoo in C++, and a first Monte Carlo

The review engine will judge a move by playing the rest of the game out many times. Who plays those games matters: if every simulated opponent played randomly, a move that only works against random players would look good. catan-project already has bots that play the way different people do, the hard bot and its **zoo** (archetypes, trading personalities, shaken weights; see catan-project's `docs/bot-zoo.md`). They are written in Go and play over a websocket, about 45 seconds per game in a tournament. This step ports them to C++, move for move, so a simulation can call them a million times.

Code: `include/catan/zoo.hpp`, `src/zoo.cpp`. Tools: `catan_zoo_check`, `catan_selfplay --bots zoo`, `catan_winrate`.

## What was ported

`zoo_decide(game, seat, bot, memory)` asks the questions `Decide()` in `strategy.go` asks, in the same order: a discard the seat owes, then an offer to answer, then its own turn (setup spots, the defensive knight, the robber and the victim, free roads, and the main phase: build, play a card, road toward a target, buy a card, trade with players, then the bank, spend before a 7). Every function in `zoo.cpp` names the Go function it ports.

The bot sees what its seat sees on the platform: the board, its own hand, everyone's visible points, card counts, knights and road lengths. It never reads another hand or the deck.

### Where the order matters

The hard bot often takes "the first of equally good options". Ties are common (two spots with the same pips, two roads that both lead to the target), so to make the same move the C++ bot has to see the options in the same order as the Go bot:

| Option list | Order on the platform | In C++ |
|---|---|---|
| settlement spots, roads | iteration order of a Python `set` of positions | a table (`kServerNodes`, `kServerEdges`) printed from `Board.vertices()` / `edges()`; fixed, since small integer tuples hash the same in every run |
| spots for a city | insertion order of the `settlements` dict, i.e. the order the settlements were built | `TableMemory::built`, kept as the game goes |
| corners when looking for a target | sorted by their coordinates (board.go) | `corner_order()`, the same coordinates computed once |
| tiles around a corner | hex order | hex order |
| robber hexes, steal victims | hex order, seat order | the same |

Floating point sums are written in the same order as in Go, so they round to the same bits.

### What the Game struct does not keep: `TableMemory`

Three things the Go bot knows come from the platform's log and lists, not from the rules state:

- how many trades the current player has offered this turn (the bot counts `"<name> offers a trade."` lines since `"<name>'s turn."`);
- in which order players accepted the open offer (it confirms the first). Answering again moves a player to the end, as `respond_to_trade` in game.py does;
- in which order settlements were built (above).

`play_zoo_step()` keeps the memory for its games; `catan_zoo_check` rebuilds it from a record.

### Shaken weights

A zoo bot multiplies every weight by `exp(0.2 z)`, `z` standard normal. The C++ zoo draws `z` from our own `Rng` as the sum of 12 uniforms minus 6 (mean 0, variance 1), and computes `exp` from its Taylor series. Only `+ - * /` go into a weight, so the native and WebAssembly builds get exactly the same bot from a seed (`std::exp` may round differently in glibc and in Emscripten's libc). The spread is that of the Go zoo (a test checks mean 0 and standard deviation 0.2 of `log(weight)`), but the same seed gives a different bot than in Go, because the generators differ. A recorded bot is rebuilt from the shaken weights its record carries (`zoo_bot_from_record`), so the comparison with Go needs no Go generator.

## Does it play like the Go bots?

`catan_zoo_check` replays tournament records. Before every move a hard or zoo bot made, it asks the C++ bot what it would do in the same position and compares. Chance is not a decision: for a roll it compares that the bot rolled, for a steal whom it robbed.

The records are noisy. The platform runs in real time, and the tournament log of the 400 zoo games holds about 34 000 moves the server refused, almost all of them races: an answer to an offer that was already withdrawn (18 400), a cancel of an offer already closed (12 600), a move after the turn had passed. A Go bot thinks on the newest state it has received, which can be a few moves old, may send a move twice, and after a refusal falls back to a safe move. So each recorded move falls in one of four groups:

- **same**: the C++ bot makes this move in the position just before it;
- **earlier**: it makes it in an earlier position of this turn or the one before, or since the offer before the open one;
- **fallback**: a safe move after a refusal: `cancel_trade` or `end_turn` in the main phase, or a roll instead of a knight;
- **differ**: none of these, a real difference between the bots.

On every record catan-project's 9b tournaments wrote (400 zoo games, 200 games of four hard bots):

| Move | same | earlier | fallback | differ |
|---|---:|---:|---:|---:|
| build_settlement | 9 532 | 0 | 0 | 0 |
| build_road | 15 493 | 0 | 0 | 0 |
| upgrade_city | 5 165 | 0 | 0 | 0 |
| roll_dice | 45 016 | 0 | 0 | 0 |
| discard | 6 229 | 0 | 0 | 0 |
| move_robber | 10 607 | 0 | 0 | 0 |
| steal | 10 503 | 0 | 0 | 0 |
| trade_with_bank | 10 938 | 5 | 0 | 0 |
| offer_trade | 37 015 | 64 | 0 | 0 |
| respond_trade | 86 643 | 87 072 | 0 | 0 |
| confirm_trade | 5 637 | 3 | 0 | 0 |
| cancel_trade | 22 962 | 2 689 | 171 | 0 |
| buy_dev_card | 6 465 | 0 | 0 | 0 |
| play_knight / road building / year of plenty / monopoly | 4 256 | 0 | 0 | 0 |
| end_turn | 33 950 | 157 | 10 322 | 0 |
| **all** | **310 411** | **89 990** | **10 493** | **0** |

What this shows: 310 000 decisions made in the very position before them are identical, including every build, robber, discard and card. Most "earlier" moves are answers to offers sent two or three times (the log shows the same seat answering the same offer repeatedly). What it does not show: a move in the "earlier" or "fallback" group could hide a difference, since the check accepts it on weaker evidence. A cleaner test would feed the Go bot the platform's view of a position directly and compare the answers with no clock in between; the 310 000 "same" decisions make that less urgent.

The first run of this check found three real bugs in the port, each a detail of the platform rather than of the bot: the order of settlement spots for cities (3 differences), the order of acceptances after an answer is changed (1), and that the server lists positions in Python's set order (before the table, most road and settlement choices with a tie differed).

`ctest` runs the check on three records in `tests/data`; `scripts/check.sh` runs it on all of them when catan-project's `.logs/zoo-data` is there.

### Statistically

Win rate per seat by archetype, Go tournament (400 games, about ±4 points) against `catan_selfplay --bots zoo --games 2000`:

| | ore-wheat | balanced | wood-brick | port |
|---|---:|---:|---:|---:|
| Go | 30.0% | 27.6% | 23.6% | 19.1% |
| C++ | 30.9% | 27.9% | 21.8% | 19.2% |

Games last 74.6 turns in Go and 78.0 in C++. The C++ games have no clock: no stale answers and no offers withdrawn after a timeout, so the trading runs a little differently.

## Speed

One core, i7-11800H, 4 players, `catan_selfplay` (laptop numbers vary by ±15% between runs):

| Players | games/s | µs per move |
|---|---:|---:|
| random | ~10 400 | 0.15 |
| hard | ~1 400 | 1.1 |
| zoo | ~2 200 | 0.85 |

Threads, zoo: 1: 2 100, 2: 4 400, 4: 6 100, 8: 8 000, 16: 9 600 games/s. A zoo game is about five times as expensive as a random one: every decision builds its plan again (production, scarcity, a breadth-first search for the target, the goal), and the hard bot's games are longer with more trading. Not profiled yet; the obvious candidate is computing the plan once per turn instead of once per decision.

The WebAssembly build plays the same zoo games (`node web/scripts/selfplay-node.mjs --bots zoo` prints the native checksum; `check.sh` compares them) at about 1 100–1 300 games/s in Node.

## A first Monte Carlo: `catan_winrate`

At the start of every turn of a record, `catan_winrate` plays the position out to the end many times with zoo bots and reports each seat's share of wins:

```sh
build/release/tools/catan_winrate --playouts 400 --threads 16 game.json
build/release/tools/catan_winrate --calibrate 4 --playouts 200 --threads 16 records/*.json
```

A playout keeps the position (board, buildings, hands, held cards, points, awards) and draws the rest anew: dice, stolen cards, the order of the undrawn development cards, and the bots: each seat becomes a new random zoo bot (`--bots zoo`, the default), or keeps the bot that sat there (`--bots record`). Hands are those of the record, so this is a spectator's view; a player's own view, with hidden cards drawn from what they could know, comes with the move review.

With several records it checks calibration: of all the times it said about 30%, how often did that player win? On 60 zoo games (4 313 positions, 100 playouts each):

| Said | Mean said | Won | Count |
|---|---:|---:|---:|
| 0–10% | 3.5% | 4.9% | 5 583 |
| 10–20% | 14.2% | 14.8% | 3 310 |
| 20–30% | 24.1% | 21.6% | 2 487 |
| 30–40% | 34.2% | 35.1% | 1 847 |
| 40–50% | 44.1% | 43.7% | 1 460 |
| 50–60% | 54.0% | 53.7% | 888 |
| 60–70% | 64.2% | 64.2% | 581 |
| 70–80% | 74.1% | 70.9% | 440 |
| 80–90% | 84.5% | 78.8% | 311 |
| 90–100% | 95.8% | 92.2% | 345 |

Brier score 0.139, against 0.1875 for always saying 25%. With `--bots record` (the real bots) it is 0.135.

Read with care:

- These are games the zoo itself played, so the simulation's opponents are the real opponents. That is the easy case; human games are the real test.
- The positions of one game are not independent: 70 positions of the same game share one winner. The buckets look more certain than they are.
- 100 playouts give a standard error of up to 5 points per estimate, which spreads the buckets.
- The top buckets are slightly overconfident (said 85%, won 79%).

This first sketch is superseded by [win-chances.md](win-chances.md): points before and after every roll, the split into luck and decisions, and calibration with intervals from a bootstrap over games, which confirms the overconfidence at the top.
