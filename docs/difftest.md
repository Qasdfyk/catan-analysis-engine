# Difference test

The C++ rules must do exactly what catan-project's `game.py` does: the engine will judge moves made on the platform, and a rule that differs even rarely makes some judgments wrong. Reading two rule engines side by side does not find such differences reliably; running both on the same games does.

`tools/difftest.py` loads the engine as a Python module (`cae_py`, built with [pybind11](https://pybind11.readthedocs.io/) from `python/bindings.cpp`) into the Python of the catan-project backend, so both engines live in one process. After **every** action it compares:

- the whole state: hands, buildings, roads, development cards, phase, setup queue, pending discards, robber, open trade, deck, awards, road lengths, winner;
- the legal moves of every seat: settlement spots, road sides, cities, robber hexes, steal victims, playable cards, whether a card can be bought.

The first difference stops the run and prints its path, for example `$.players[1].resources[2]: python 0 vs c++ 1`.

```sh
cmake --preset python && cmake --build --preset python
../catan-project/backend/.venv/bin/python tools/difftest.py --games 20
```

`scripts/check.sh` runs it after the C++ tests (about 25 s with 20 games). It needs catan-project checked out next to this repo.

## What it plays

1. **Topology.** Every corner and side of the Python board maps to one engine index, with the same neighbors and ends, and all 222 spellings within three rings (land and sea hexes) agree.
2. **Records.** `tests/data/*.json` (simulated games of 2, 3 and 4 players) and every game the local platform saved in `backend/records/`, event by event.
3. **Random games.** catan-project's random player plays 2 to 4 seats with random house rules (victory points 8 or 10, discard limit 7 or 9, friendly robber on or off). About 30% of the steps are instead **probes**: a random action with random arguments, half of them aimed near legal ones (the robber's own hex, a corner that already has a building, the right number of cards to discard). Most probes are illegal, and both engines must refuse them alike; the rest must be accepted by both. After the end, 20 more probes must all be refused.

   Every other game also **squeezes the bank**: from time to time one player is handed nearly all the cards of one resource, in both engines, so a roll can owe more than the bank holds.

## How good is it?

A test that has never failed proves little. Two measurements:

**Coverage of `game.py`.** Under the test, `coverage` reports 93% of the lines of `game.py` executed. Most of the rest is the turn timer (`auto_finish_turn`, which records ordinary events and so needs no test of its own) and refusals that random arguments rarely reach.

```sh
../catan-project/backend/.venv/bin/python -m coverage run --include='*/app/models/game.py' tools/difftest.py --games 20
../catan-project/backend/.venv/bin/python -m coverage report -m
```

**Planted bugs (mutation testing).** Twelve small bugs were put into `src/game.cpp` one at a time, each a plausible slip: longest road from 4, a tie taking Largest Army, discarding half rounded up, the friendly robber off by one, a 2:1 port acting as 3:1, an opponent's settlement not cutting a road, and so on. Results:

| | Caught |
|---|---|
| First version of the test | 9 of 12 |
| With the bank squeeze, aimed probes and steal probes | 11 of 12 |
| Together with the C++ unit tests | 12 of 12 |

The first version missed the short bank rule: random play never empties the bank, and when the bank did hold 0 cards, "nobody gets any" and "everyone gets what is left" give the same result. The squeeze leaves 1 to 3 cards for that reason. Year of Plenty against an empty bank still escapes: `game.py` checks the bank before checking that the player holds the card, so a random probe nearly always fails for the other reason too. `tests/game_test.cpp` covers it, with the other rules that random play reaches too rarely.

Nothing was found in `game.py` or the C++ rules by the test itself: the engine agreed with Python from the first run, on all 17 recorded games (9 385 events) and the random games. The planted bugs are what show the agreement means something.
