# Arena and test sets

Step 10b of the plan: the bench to test on before any training. Four parts:

- **the arena** (`catan_arena`), matches between bots with a sequential test that stops as soon as the result is clear;
- **Catanatron** as an outside opponent, through a bridge to a separate process;
- **set A**, a frozen set of positions to score win chances on, and a **baseline model** that anything learned has to beat;
- **sanity positions C**, positions with an obvious right answer.

Code: [`arena.hpp`](../include/catan/arena.hpp), [`tools/arena.cpp`](../tools/arena.cpp), [`tools/catanatron_bridge.py`](../tools/catanatron_bridge.py), [`tools/validate.cpp`](../tools/validate.cpp), [`calibration.hpp`](../include/catan/calibration.hpp). `scripts/check.sh` runs a short version of each.

## The arena

One candidate sits at a table with three opponents. In a four-player game an equal candidate wins 25%. Results are win rates with 95% intervals.

```sh
build/release/tools/catan_arena --candidate hard --opponents zoo --deals 8000 --threads 12
build/release/tools/catan_arena --candidate zoo:ore-wheat/fair --opponents hard --sprt 0.25 0.30 --threads 12
build/release/tools/catan_arena --candidate zoo:ore-wheat/fair --baseline hard --opponents zoo --sprt 0 0.03
```

Players: `hard`, `zoo` (a new random zoo bot every deal), `zoo:ARCHETYPE/PERSONALITY[:SEED]`, `catanatron` (AlphaBeta) and `catanatron:weighted`. `--no-trades` turns off trading between players.

### Deals and modes

A deal fixes the board, the development card deck, the first player and a stream of dice. Deal *i* of a seed is always the same deal, so results do not depend on the number of threads.

| Mode | A deal is | Same in the games of a deal |
|---|---|---|
| `plain` (default) | one game, candidate on a random seat | — |
| `rotate` | four games, candidate in every seat | board, deck, opponents |
| `duplicate` | four games, candidate in every seat | the same, and roll *k* is the same pair of dice in all four |

`--baseline P` plays every deal a second time with `P` in the candidate's place (same seats, opponents, dice and random streams) and reports the difference per deal: a paired comparison, like two tables of duplicate bridge playing the same cards.

### Do duplicates help?

Duplicate bridge, duplicate poker and fishtest's game pairs exist because shared luck cancels out. Whether it cancels in Catan is a measurement. `--compare-modes` plays the same number of games in each mode and prints, for each, how many games a ±1 percentage point interval (95%) needs:

```sh
build/release/tools/catan_arena --compare-modes --candidate hard --opponents zoo --deals 5000 --threads 16
build/release/tools/catan_arena --compare-modes --candidate hard --baseline zoo:ore-wheat/fair --opponents zoo --deals 5000 --threads 16
build/release/tools/catan_arena --compare-modes --candidate zoo:ore-wheat/fair --baseline zoo:wood-brick/fair --opponents hard --deals 5000 --threads 16 --seed 7
```

Results (seed 1 unless given; 20,000 games per mode and player; "paired" is the difference between candidate and baseline):

| Comparison | plain | rotate | duplicate |
|---|---|---|---|
| hard against random zoo bots, games for ±1 point | 8,928 | 9,518 | 10,083 |
| hard − ore-wheat against random zoo bots, paired | 30,395 | 32,717 | 34,665 |
| the same, if the two were separate matches | 35,919 | 39,159 | 42,068 |
| ore-wheat − wood-brick against 3× hard (seed 7), paired | 27,651 | 31,170 | 33,384 |
| the same, if separate | 27,855 | 30,015 | 32,016 |

What this says:

- **Rotation and fixed dice do not make a win rate more precise per game**; if anything they cost a little. A game's outcome is 0 or 1 and its variance is p(1 − p) whatever the seat. Rotating only helps if the four games of a deal are *negatively* correlated (a deal good for seat 2 is bad for the others, so the candidate wins it once out of four). They are not: the four games share the opponents and the board, which makes them slightly *positively* correlated.
- **Fixed dice do not keep games together.** After a different move the bots hold different cards, so the same roll means something different within a turn or two, as with the common random numbers of [win-chances.md](win-chances.md).
- **Pairing helps only when the opponents vary** (1.2× fewer games with random zoo opponents, which both players meet, nothing against fixed hard bots).

So `plain` is the default, `--baseline` is worth it when opponents are drawn at random, and the real savings must come from elsewhere: from SPRT (below), and later from a value network that can subtract the luck of each roll from the result (control variates; AIVAT does this for poker).

### SPRT

`--sprt P0 P1` is Wald's sequential probability ratio test, as fishtest uses it for Stockfish patches. After every batch of deals (`--batch`, default 64) the log-likelihood ratio of the results under H1 (win rate P1) and H0 (P0) is compared with the bounds ln(β/(1−α)) and ln((1−β)/α), ±2.94 at α = β = 0.05. A deal's score is the share of its games the candidate won, which is not a 0-or-1 outcome, so the ratio uses a normal model of the deal scores with their measured variance (the generalized SPRT of fishtest's pentanomial model):

    LLR = n (P1 − P0) (2·mean − P0 − P1) / (2·var)

With `--baseline`, P0 and P1 are differences (for example `--sprt 0 0.03`: "no better" against "3 points better").

Examples (seed 1, 16 threads):

```
hard against zoo, --sprt 0.25 0.30:         H1 accepted after 256 deals (true win rate about 37%)
ore-wheat − hard against zoo, --sprt 0 0.03: H1 accepted after 3,520 deals (difference +2.7 ± 2.1 points)
```

The bigger the true difference compared with P1 − P0, the sooner it stops; a difference near the middle of P0 and P1 takes longest.

### Results

`catan_arena --deals 8000 --threads 12`, plain, seed 1. ± is the 95% interval.

| Candidate | Against 3× | Wins | Games |
|---|---|---|---|
| hard | hard | 24.5% ± 0.9 | 8,000 |
| hard | random zoo bots | 37.0% ± 1.1 | 8,000 |
| zoo ore-wheat/fair | hard | 27.4% ± 1.0 | 8,000 |
| zoo wood-brick/fair | hard | 19.9% ± 0.9 | 8,000 |
| zoo port/fair | hard | 15.2% ± 0.8 | 8,000 |
| zoo balanced/loner (never trades with players) | hard | 17.6% ± 0.8 | 8,000 |
| Catanatron AlphaBeta | hard, no trades | 6.5% ± 2.4 | 400 |
| hard | Catanatron AlphaBeta, no trades | 52.0% ± 6.9 | 200 |

Under our rules and without trading, the hard bot is clearly stronger than Catanatron's AlphaBeta, from both sides: AlphaBeta wins 6.5% against three hard bots, and the hard bot wins 52% against three AlphaBetas (25% would be even). See the checks and caveats under Catanatron below before reading much into it.

The first row is the control: the hard bot against itself wins 25% within the interval. Hard beats the zoo on average because a random zoo bot is often a port or loner bot, which are weak; "balanced/loner" is the hard bot that refuses to trade, and it loses a third of its wins.

## Catanatron

[Catanatron](https://github.com/bcollazo/catanatron) (GPL-3.0) is the strongest public Catan bot; its `AlphaBetaPlayer` (depth 2) leads its own leaderboard, and the HexMachina paper of 2025 reports 54% against it. It is the only opponent here that we did not write.

**Our engine is the referee.** For every Catanatron decision the arena sends our position to [`tools/catanatron_bridge.py`](../tools/catanatron_bridge.py) as JSON (board, snapshot, pieces in the order they were built); the bridge builds a Catanatron state from it, asks Catanatron for a move and translates it back. Nothing is kept between decisions, so the two engines cannot drift apart. The bridge is a separate process that imports Catanatron from where it is installed: no GPL code is in this repository.

The translation:

- Hexes: our axial (q, r) is Catanatron's cube (q, −q−r, r), and corner d of our hex is Catanatron's NE, SE, S, SW, NW, N. The bridge checks on start that all 19 hexes, 54 corners and 72 sides map one to one.
- Ports go where our board has them, as Catanatron port tiles on the sea side that touches the same two corners.
- Catanatron discards one card per decision; ours discards all at once, so the bridge asks card by card.
- Catanatron moves the robber and names the victim in one move; ours takes two, so the victim comes back with the robber move.
- Catanatron has no trading between players, so matches with it use `--no-trades`: zoo bots make no offers and decline any.

**Checks.** Every move Catanatron made in the matches above was accepted by our rules (0 refused, 0 untranslatable, over 64,000 decisions). With `CATANATRON_BRIDGE_CHECK=1` the bridge compares Catanatron's legal settlements, cities, roads and development card purchases with ours at every decision: no difference in 4,300 decisions (48 games with WeightedRandom, 32 with AlphaBeta), so it is not missing moves it could make. Through the bridge, AlphaBeta beats three of Catanatron's own WeightedRandom players in 64 of 64 games; in Catanatron's own engine it wins 63 of 64. So the bridge does not weaken it.

**Caveats.** The rules are catan-project's (they differ from Catanatron's in small ways, such as the discard limit and when development cards may be played). Catanatron's state has no hidden information, so it sees every hand and the order of the development card deck; our bots see only what a player sees. And trading between players, which is worth a lot to our bots, is off.

Install (the PyPI release 3.2.1 lacks `AlphaBetaPlayer`; the source has it):

```sh
git clone https://github.com/bcollazo/catanatron /tmp/catanatron-src
git -C /tmp/catanatron-src checkout ecf9311
uv venv /tmp/catanatron && VIRTUAL_ENV=/tmp/catanatron uv pip install /tmp/catanatron-src
build/release/tools/catan_arena --candidate catanatron --opponents hard --no-trades --deals 100 --threads 16 \
    --python /tmp/catanatron/bin/python
```

AlphaBeta takes about 0.05 to 1 s per decision, so a game with one Catanatron seat takes 10 to 20 s of one core. `CATANATRON_BRIDGE_LOG=file` writes every message and reply to a file.

## Set A and the baseline model

Set A is 4,000 positions from 1,000 games of native zoo self-play (`catan_selfplay --bots zoo`, seed 1001), four points just before a roll per game, with the winner. It is stored in [`data/set-a.jsonl.gz`](../data/set-a.jsonl.gz) (440 KB): one game per line, with the board, the positions as snapshots, the bots and the winner. It is read back with `game_from_snapshot()` (tested to give back the same position, which plays on the same way), so it does not depend on the engine version that made it. **It never goes into training.**

```sh
build/release/tools/catan_validate make --games 1000 --seed 1001 --positions 4 --threads 16 | gzip -9 > data/set-a.jsonl.gz
build/release/tools/catan_validate train --games 4000 --seed 2001 --positions 4 --threads 16 > data/baseline.json
gzip -dc data/set-a.jsonl.gz | build/release/tools/catan_validate eval --model data/baseline.json
gzip -dc data/set-a.jsonl.gz | build/release/tools/catan_validate eval --playouts 200 --threads 16 --bots record
```

**The baseline model** ([`data/baseline.json`](../data/baseline.json)) is a conditional logit: each seat gets a score w·x from eight numbers on the table (points with victory point cards, production in pips, cards in hand, development cards, knights, road length, whether it is to move, ports), and its chance is exp(score) / Σ exp(score). The chances add up to 1. It is fitted by Newton's method on 16,000 positions from other games (seed 2001), five iterations. Points weigh most (0.57 per point), then development cards (0.26), then a card in hand (0.09) and a pip of production (0.06).

| Estimator on set A | Brier score (95% interval) |
|---|---|
| Monte Carlo, 200 playouts, the game's own bots | 0.1263 (0.1212–0.1312) |
| Monte Carlo, 200 playouts, random zoo bots | 0.1301 (0.1255–0.1348) |
| baseline model | 0.1497 (0.1456–0.1541) |
| always 1 in 4 | 0.1875 |

Calibration, Monte Carlo with the game's own bots:

```
      said     mean      won          interval   count
  0- 10%      3.3%     2.8%     2.2% ..   3.4%    5573
 10- 20%     14.6%    13.1%    11.7% ..  14.7%    2856
 20- 30%     24.6%    25.5%    23.5% ..  27.4%    2219
 30- 40%     34.5%    34.5%    32.3% ..  36.9%    1640
 40- 50%     44.5%    45.3%    42.4% ..  48.1%    1169
 50- 60%     54.5%    58.0%    54.0% ..  62.1%     861
 60- 70%     64.7%    63.3%    58.8% ..  67.8%     581
 70- 80%     74.3%    77.4%    72.5% ..  82.1%     456
 80- 90%     84.8%    85.7%    80.9% ..  90.3%     308
 90-100%     95.8%    97.0%    94.7% ..  99.1%     337
```

On the platform's tournament records (step 10) the same estimate was too sure of itself at 80–90%. Here, where the games and the playouts are the same process (the same C++ bots, no network delays or fallbacks), it is not: every bucket's mean is inside its interval. The gap there came from the difference between the platform's games and the simulation, not from Monte Carlo itself. With random zoo bots instead of the game's own, three buckets fall just outside, and it is slightly too unsure of the leader (64–75% said, 69–81% won): it does not know that the leader's bot is a good one.

A value network has to beat 0.150 clearly and get near or below 0.126, at a fraction of the cost (200 playouts of 4,000 positions take about a minute on 16 threads).

## Sanity positions C

Positions with one clear answer, like EPD test suites in chess. Each scenario is set up on many real positions (the last drawn point of each of 200 zoo games, seed 3001): same idea on other boards and hands. Each checks what the bot does rather than one exact move.

| Scenario | Set up | Pass when |
|---|---|---|
| win-settlement | 9 points (victory point cards added), a settlement's cards, a free spot | it wins this turn |
| win-city | 9 points, a city's cards | it wins this turn |
| win-army | 8 points, 3 knights against the holder's 3, a knight to play | it wins this turn |
| refuse-leader | a clear leader with 8 visible points offers ore for sheep | it says no |
| robber-leader | it moves the robber; a clear leader | the robber touches the leader and not itself |
| steal-leader | it can rob the leader and someone else | it robs the leader |
| first-spot | first settlement of a new game | a corner among the 5 richest by pips |

The leader is given Longest Road and Largest Army to reach 8 visible points and must be strictly ahead of everyone.

`catan_validate sanity --bot hard --games 200 --seed 3001`:

| Scenario | hard | ore-wheat/loner | wood-brick/friendly |
|---|---|---|---|
| win-settlement | 137 / 137 | 137 / 137 | 137 / 137 |
| win-city | 199 / 199 | 199 / 199 | 199 / 199 |
| win-army | 192 / 192 | 192 / 192 | 192 / 192 |
| refuse-leader | 148 / 148 | 148 / 148 | 107 / 148 |
| robber-leader | 132 / 148 | 132 / 148 | 132 / 148 |
| steal-leader | 147 / 147 | 147 / 147 | 147 / 147 |
| first-spot | 181 / 200 | 153 / 200 | 169 / 200 |

The friendly personality takes a leader's trade by design (no embargo). The robber misses are a real weakness of the hard bot: it weighs the harm to everyone on a hex, so two middling players can outweigh the leader. The expert bot (step 12) has to pass all of them.
