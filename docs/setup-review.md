# Setup review: the initial placement, corner by corner

The first decision the engine grades. For each settlement a seat places in the setup, every corner it could legally have taken is scored by its chance to win, and the corner played is compared with the best one. Code: `include/catan/setup_review.hpp`, `src/setup_review.cpp`, the tool `tools/setup.cpp` (`catan_setup`), and in the browser `web/src/setup.ts` with `components/SetupReview.tsx`.

## What a corner is worth

The settlement goes on the corner, and from there zoo bots play the game out to the end: the road of that settlement, the rest of the setup, the whole game. The seat's share of wins is the corner's value. Every seat of every playout is a MAP-Elites bot drawn from `data/elites.json` (docs/map-elites.md), the seat under review included: the review answers "how good is this corner for a strong player of our pool", not "for the person who played it". Playouts come from `play_out_chances` (docs/win-chances.md), so playout *i* uses the same random stream for every corner.

## Successive halving

About 50 corners are legal for the first settlement. The good ones differ by a few percentage points; with *n* playouts an estimate near 25% has a standard error of 43/√n points (4.3 at 100 playouts, 1.1 at 1600). Giving every corner 1600 playouts would cost 80 000 per move, most of it on corners that are clearly bad after 100.

Successive halving ([Karnin et al. 2013](https://proceedings.mlr.press/v28/karnin13.pdf), [Jamieson and Talwalkar 2016](https://arxiv.org/abs/1502.07943)) spends the playouts where the decision is:

| round | corners in | new playouts each | playouts in the round |
|---|---|---|---|
| 0 | 54 | 100 | 5 400 |
| 1 | 27 | 200 | 5 400 |
| 2 | 14 | 400 | 5 600 |
| 3 | 7 | 800 | 5 600 |
| confirm | 4 (+1) | 1 600 | 6 400–8 000 |

(The first settlement of a game: 54 legal corners. Later ones have fewer.) About 28 000 playouts per move, a third of the flat cost, and the corners that matter end with 1 600–3 100 playouts each.

Two rules beyond plain halving:

- **The corner played is never dropped.** It is the one being graded, so its value needs the same precision as the best one's. If the halving would drop it, it stays as an extra finalist.
- **A confirmation round.** The finalists are the corners whose estimates came out highest, and part of that is noise that favoured them: the maximum of noisy estimates is biased upwards (the optimizer's curse, [Smith and Winkler 2006](https://www.gwern.net/doc/statistics/decision/2006-smith.pdf); the winner's curse of docs/map-elites.md). So the halving only *chooses* the best corner; its value, and the value of the corner played, come from 1 600 fresh playouts (indices from 2^24 up) that played no part in the choice. The loss is

  loss = confirmed value of the best corner − confirmed value of the corner played,

  with a standard error from the two binomial estimates counted as independent. They share playout streams, which makes them positively correlated, so the true error is somewhat smaller and the interval errs on the safe side. The loss can come out negative: then the corner played did better in the confirmation, and it is as good as the best.

The verdict on the page: **best** (the corner played is the best one), **within noise** (loss below 1.96 standard errors) or **loses**.

## Native and browser give the same review

The review runs round by round (`SetupReview::step`), so the web page can draw the heat map after every round. Each Web Worker reviews whole moves; a game of four has eight setup moves, so up to eight workers help. The elites are bundled into the worker (`?raw` import of `data/elites.json`), and `setElites()` hands them to the WebAssembly module. `scripts/check.sh` reviews the same game natively and in WebAssembly under Node with a small budget and compares a checksum of every win count of every corner: equal, so the browser plays exactly the playouts of the native build.

## Results on recorded games

`catan_setup --summary --threads 16` on 120 tournament records of catan-project (`.logs/zoo-data`): the first 60 games of four hard bots and the first 60 zoo games. 960 setup moves, 22.9 million playouts in 31 minutes (12 300 playouts/s on 16 threads of an i7-11800H; a playout from the setup is a whole game, about twice as long as the average point of the chance curve). Brackets are 95% intervals from 1 000 bootstrap draws of games.

| player | moves | mean loss per settlement | took the best corner | best or within noise |
|---|---:|---:|---:|---:|
| hard | 480 | 5.3 pp [4.7, 6.0] | 30.6% [26.9, 34.7] | 47.3% [43.1, 51.6] |
| zoo | 480 | 7.9 pp [7.0, 9.0] | 27.7% [24.3, 31.5] | 40.0% [35.5, 45.0] |

The hard bot's setup rule (`hard_setup` and `spot_value` in `src/zoo.cpp`: score every corner by its pips, weighted up for resources that are rare on the board or not yet collected, plus a port, and take the top) is a good heuristic but not the engine's choice: it takes the best corner in about a third of the settlements and loses about 5 points of chance to win per settlement, some 11 points over its two. The zoo, whose archetypes weight resources on purpose (ore-wheat, wood-brick, port), loses more. Part of any loss is the model, not the move: the corners are valued for elites, and a corner that suits a wood-brick bot may be worth less to the average elite.

**The winner's curse.** On the moves where the corner played was not the best, the loss from the halving's own playouts is higher than the confirmed one by 0.35 pp [0.18, 0.53]. Small, because the finalists already carry 1 500–3 100 playouts each, but systematic: without the confirmation every loss would come out slightly too high.

**Does a better setup win more often?** Per seat, the loss of both settlements together:

| | win rate |
|---|---:|
| the seat that lost least in its game | 40.8% [32.5, 49.2] |
| the seat that lost most in its game | 19.2% [12.5, 26.7] |
| (no effect) | 25% |

| quarter of seat loss | seats | win rate |
|---|---:|---:|
| −3.4 .. +4.6 pp | 120 | 31.7% [25.0, 39.3] |
| +4.6 .. +11.3 pp | 120 | 28.3% [21.3, 34.7] |
| +11.3 .. +19.6 pp | 120 | 25.0% [18.8, 32.2] |
| +19.6 .. +53.5 pp | 120 | 15.0% [9.6, 21.2] |

So the review measures something real: in games it never played, the seats it grades worse win less, monotonically by quarter. The effect is a rough one (the intervals overlap between neighbouring quarters), as it should be for one decision out of a whole game, and these are games between bots; games of people will be the real test (human set H, see [plan.md](plan.md)).

## What it does not do yet

- The review takes the spectator's view of a position, like the chance curve: the bots in the playouts are the pool, not the players of the game.
- The road of a setup settlement is not graded; the bot of the playout places it.
- One step in the future the move review (step 13 of the plan) grades every move this way, with search instead of flat playouts per candidate.
