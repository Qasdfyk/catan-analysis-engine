# MAP-Elites over the zoo

Step 10c of the plan. The zoo of [zoo.md](zoo.md) has four archetypes and four trading personalities whose weights were set by hand. The simulations of [win-chances.md](win-chances.md) draw their opponents from it, so it should play every style as well as that style can be played. Tuning the weights for wins alone would turn every bot into the one strongest style. MAP-Elites (Mouret and Clune, 2015, [arXiv:1504.04909](https://arxiv.org/abs/1504.04909)) gets strength and variety at once: it cuts the space of styles into cells and keeps, in every cell, the strongest bot seen that plays in that style.

Code: [`map_elites.hpp`](../include/catan/map_elites.hpp), [`map_elites.cpp`](../src/map_elites.cpp), [`tools/map_elites.cpp`](../tools/map_elites.cpp), elites as JSON in [`elites.hpp`](../include/catan/elites.hpp). The archive is [`data/elites.json`](../data/elites.json).

## The method

A bot is a genome: the zoo's weights (five resources, variety, port, city, settlement, development card, roads behind the Longest Road), its trading (offers per turn, whom it refuses, whether it takes an even trade, whether it trades at all) and one new gene, the Robber's leader focus (below).

The loop:

1. **Seed bots**: the 16 hand-made zoo bots, unshaken, and 64 random shaken ones.
2. **Candidates**: each generation, 128 mutated copies of the elites of random filled cells. A weight is multiplied by exp(0.25 z) with probability 1/2, whole-number genes move one step now and then, yes/no genes flip rarely.
3. **Search deals**: every candidate plays the same 400 arena deals (plain, one game each, a random seat, three random zoo bots). Its style decides its cell; its win rate is its strength.
4. **Recheck**: a candidate that beats its cell's elite there plays 800 fresh deals of its own, and takes the cell only if it beats the elite on those too.
5. **Re-evaluation**: every generation each elite plays 100 more fresh deals and adds them to its score.
6. **Final**: after the last generation every elite plays 4,000 deals no choice was made on.

### The style is measured

A cell is given by what the bot does in its games, averaged:

| Axis | Measure | Bins |
|---|---|---|
| cities | share of its building points from cities (2 per city, 1 per settlement) at the end | < 45%, 45–60, 60–72, 72–84, 84%+ |
| trades | trades with players completed per game, as proposer or partner | none (< 0.5), 0.5–3, 3–7, 7+ |
| development cards | bought per game | < 1.5, 1.5–3, 3–5, 5+ |

5 × 4 × 4 = 80 cells. The edges come from the spread of the hand-made zoo (`catan_map_elites --describe`): its bots span city shares of 0.39 to 0.89, 0 to 11 trades and 1 to 6 cards a game. Declared styles (the weights themselves) would be simpler and free of noise, but a bot with a high city weight can still play like any other; the measured style is what an opponent actually meets.

## The winner's curse

400 games give a win rate to about ±4 points (95%). The elite of a cell is the bot that looked best, and looking best includes luck, so the score an elite carries is too high. This is the optimizer's curse of decision analysis (Smith and Winkler, 2006): choosing by noisy estimates guarantees disappointment on average.

Plain MAP-Elites stores the search score. The recheck alone is not enough: its score was also used to choose (a candidate takes the cell when the recheck says so), so it is cursed too, only less. Re-evaluation helps more: games played after a choice carry none of that choice's luck, so a lucky elite sinks back toward what it is worth and the next good candidate takes the cell. This is the archive re-evaluation of noisy quality-diversity (Flageat and Cully, 2023, [arXiv:2302.00463](https://arxiv.org/abs/2302.00463)). Only the final deals are honest, which is why the page and every table here show final scores.

How large the curse is with and without the recheck and re-evaluation is a measurement you can run yourself (two commands, a few minutes each):

```sh
build/release/tools/catan_map_elites --generations 20 --recheck 0 --reeval 0 --threads 16 --out /tmp/plain.json
build/release/tools/catan_map_elites --generations 20 --threads 16 --out /tmp/fresh.json
```

Each prints `mean score above final` at the end. [Results below](#measured-curse).

## Results

`catan_map_elites --threads 16` (seed 1, 100 generations of 128, 400 search games, 800 recheck, 100 re-evaluation, 4,000 final): 12,880 candidates, 7.4 million games in 12 minutes (10,300 games/s). 79 of the 80 cells hold an elite; the empty one is many cities, many trades and few development cards.

Final scores, all on the same 4,000 deals against three random zoo bots (`--describe 4000 --seed 1001` plays the hand-made zoo on the same deals; 25% is even, ± about 1.5):

| Bot | Style (cities, trades, dev cards a game) | Wins |
|---|---|---|
| best elite (from ore-wheat/friendly) | 61%, 5.7, 6.4 | 46.2% |
| best hand-made: ore-wheat/fair | 90%, 5.3, 6.1 | 37.7% |
| hard (balanced/fair) | 73%, 5.4, 2.5 | 36.0% |
| mean of the 79 elites | | 33.0% |
| mean of the 16 hand-made bots | | 26.6% |

The best elites come from ore-wheat/friendly, which by hand was one of the weaker personalities (30%). Mutation kept its three offers a turn and its trading with anyone, leader included, and dropped the one habit that cost it: taking trades that leave its goal no closer (`neutral`). The friendly personality was weak because it is a pushover, not because it trades with the leader.

The archive as opponents is much harder than the hand-made zoo:

| Match (`catan_arena`, 8,000 deals, plain) | Wins |
|---|---|
| hard against 3 random zoo bots | 37.0% ± 1.1 |
| hard against 3 random elites (`--opponents elites`) | 28.0% ± 1.0 |
| best elite (`elite:0`) against 3× hard | 33.7% ± 1.0 |
| best elite against 3 random elites | 35.1% ± 1.0 |

The best hand-made bot managed 27.4% against 3× hard ([arena.md](arena.md)). `catan_arena --candidate elite:0 --opponents hard --sprt 0.25 0.30` accepts H1 after 128 deals.

Elites play in the arena (`elite:N`, `elites`), in `catan_winrate --bots elites` and in `catan_validate sanity --bot elite:N`. The platform's Go zoo is unchanged: the elites live in the engine for now.

## The Robber and the leader

The sanity positions C of [arena.md](arena.md) found that the hard bot puts the Robber next to a clear leader in 132 of 148 positions. The cause is in `hard_robber()`: the harm of a hex sums threat × (pips + 1) over every opponent building there, doubled for cities, and the leader's threat (1 + points + 2, + 4 near the end) does not outweigh two cities of middling players on a rich hex.

The new gene `leader_focus` multiplies the leader's buildings by 1 + leader_focus. At 0 the sums stay whole numbers and every choice is the same as before, so the hard bot and the Go parity of `catan_zoo_check` do not change.

| leader_focus | robber-leader (sanity C) |
|---|---|
| 0 (hard) | 132 / 148 |
| 0.5 | 143 / 148 |
| 1 | 147 / 148 |
| 2, 4 | 148 / 148 |

But does it win more? Paired by deal (`--candidate hard+leader=1 --baseline hard`, 40,000 deals): **−0.1 ± 0.2 points** against 3× hard and the same against random zoo bots. Blocking the leader helps the other two players as much as the one who does it, so it does not show in the bot's own win rate. The sanity scenario tests a sensible habit, not a stronger move; the hard bot stays as it is. In the archive the gene drifted (48 of 79 elites have some focus, mean 0.39) without any pull either way, as a neutral gene does.

## Measured curse

Read this after the lesson's task. Seed 1, 20 generations, 16 threads:

| Variant | search above final | fresh above final | mean final of elites | cells |
|---|---|---|---|---|
| plain (`--recheck 0 --reeval 0`) | +3.4 points | — | 30.5% | 75 |
| recheck only (an earlier version) | +1.1 | +1.9 | 31.3% | 75 |
| recheck and re-evaluation (default) | +0.8 | +1.5 | 32.1% | 72 |

In the full run of 100 generations the fresh score is +1.2 points above the final one after some 3,600 fresh games per elite. The curse shrinks but never reaches zero while the same score is used to choose; only deals no choice was made on are honest. What matters more is the last column: with re-evaluation the elites really are stronger, because lucky bots do not hold their cells.
