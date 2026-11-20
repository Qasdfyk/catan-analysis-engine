# The searching bot (ISMCTS)

Step 12 of the plan: the first bot that searches. The zoo bots ([zoo.md](zoo.md)) follow rules; this one asks, for each move it could make, how often it wins after it, and answers by playing the game out from there with zoo bots.

Code: [`belief.hpp`](../include/catan/belief.hpp) (what a seat knows of the other hands, and hands drawn to match it), [`mcts.hpp`](../include/catan/mcts.hpp) (the search), [`tools/mcts.cpp`](../tools/mcts.cpp) (`catan_mcts`: what the bot thinks of the decisions of a recorded game), and `mcts[:N]` as a player of `catan_arena` ([arena.md](arena.md)).

## What it searches

Its own decisions that shape a game: both setup settlements and roads, every move of its turn (build, buy, play a card, trade with the bank, end the turn), the robber and the victim, and the knight before a roll. It leaves to its zoo bot (the hard bot) what is either forced or a reply: discarding after a 7, answering an offer, confirming or withdrawing its own. An offer to the players enters the search only as the zoo bot's own move: there are thousands of possible offers, and the zoo bot already knows which ones others take.

## One iteration

1. **Determinize.** Draw a full state that agrees with what the seat knows (next section): the opponents' hands, their development cards and the order of the deck come out differently every iteration.
2. **Select.** From the root, while it is still this seat's searched decision, pick a move among those legal in the drawn state by PUCT (below) and play it. A roll or a steal on the way draws its outcome.
3. **Expand.** The first move into a node never visited ends the walk.
4. **Play out.** Zoo bots play the rest of the game: the seat's own hard bot for it, and for each other seat a bot drawn from the MAP-Elites ([map-elites.md](map-elites.md)).
5. **Back up.** 1 for a win, 0 otherwise, added to every node on the path.

After the last iteration the bot plays the root move visited most. The tree covers the seat's own moves until its turn (or robber move) ends; everything the others do afterwards is in the playout. So this is ISMCTS over the bot's own turn, not the full multi-player tree of Cowling et al.: with a few hundred playouts per decision, tree nodes for the opponents' replies would each get a handful of visits and add noise, not knowledge.

The tree is **open loop**: a node is a sequence of the bot's moves, not a state. After the same "roll" the drawn dice differ, so the moves legal below differ too. Every child keeps an **availability count**, how many times it was legal when its parent was visited, and the exploration term uses it instead of the parent's visits (Cowling, Powley and Whitehouse 2012, section on subset-armed bandits).

## What the seat knows

At a real table a player sees the bank, every card count, every card that changes hands in public (production, building, trades, discards, monopolies) and every development card played. Two things are hidden: a card stolen between two other players, and which development cards a player holds (and the deck order). The bot must not use either, or its results in the arena would be a cheat.

**Resources.** The bank is public and so is the seat's own hand, so it knows exactly how many cards of each resource the opponents hold *together*: 19 − bank − own. What it does not know is the split. `HandBelief` keeps, for every observer and every other seat, a lower bound per resource: cards that seat surely holds. A public change moves the bound with it. A steal the observer did not see takes one unknown card from the victim, so every bound of the victim drops by one (it was one of them, but which one is unknown), and the thief gains a card of unknown kind. `determinize()` puts each opponent's sure cards in their hand and deals the rest of the opponents' pool at random to fill each hand to its public count.

**Development cards.** The unseen pool is the 25-card deck minus the cards played and the seat's own. `determinize()` deals each opponent as many as they hold (keeping apart the ones bought this turn, which cannot be played yet) and shuffles the rest into the deck. A hand that would already have won (hidden victory points reaching 10) is drawn again.

Two tests hold it to that (`tests/belief_test.cpp`):

- over whole zoo games, no bound is ever above the true hand, for any observer;
- **nothing hidden leaks**: two games that differ only in what the seat cannot see give the same drawn state for the same random stream, and the same search (`tests/mcts_test.cpp`).

In a zoo game more than half of the opponents' cards are known for sure at any moment; the rest is what steals hide.

## PUCT and the zoo bot as a prior

A plain UCT search gives every move the same chance at the start. In Catan that wastes the budget: a main-phase decision has 10–30 legal moves (every road, every bank trade, every year of plenty pair), and 400 playouts spread evenly leave 15–40 per move, a 95% interval of ±15–25 points on a 25% chance. The search would mostly play noise.

PUCT, the selection rule of AlphaZero (Silver et al. 2017), adds a prior P to each move:

    Q + c · P · sqrt(A) / (1 + n)

with Q the move's share of won playouts (its parent's for a move not yet tried), n its visits and A its availability. Moves with a high prior are tried first and kept longer; the others need results to catch up. AlphaZero takes P from its policy network. We have no network yet, but we have a good policy: the zoo bot. Its move gets P = 0.5 and the other moves share the other half. In step 16 a policy network takes its place.

Tuning against three hard bots at 400 playouts per decision, 600 games each on the same deals (seed 1), `--mcts-c` (± is the 95% interval):

| c | wins |
|---|---|
| 0.5 | 37.8% ± 3.9 |
| **1** | **49.8% ± 4.0** |
| 2 | 44.7% ± 4.0 |
| 4 | 38.0% ± 3.9 |

A larger c keeps the search close to the zoo bot's move; a smaller one lets it leave on less evidence. How small c may go depends on the budget: with fewer playouts per decision the averages are noisier, and the best c is larger (a smaller tuning run at 100 playouts preferred c = 2 to c = 1). The prior's share matters as much: at 100 playouts and c = 1, a zoo prior of 0.25 instead of 0.5 lost about 15 points.

The default c was picked on these deals, so its own result is flattered (the winner's curse of [map-elites.md](map-elites.md)): on fresh deals (seed 7, below) the same bot wins 45.8%, 4 points less, within the noise but in the expected direction.

## Results

`catan_arena --candidate mcts:400 --threads 16 --seed 7`, plain deals, c = 1: deals the tuning never saw.

| candidate | against 3× | wins | games |
|---|---|---|---|
| mcts:400 | hard | 45.8% ± 4.0 | 600 |
| mcts:400 | random elites | 44.8% ± 4.9 | 400 |
| mcts:400 | Catanatron AlphaBeta, no trades | 66.0% ± 9.3 | 100 |

SPRT `--sprt 0.30 0.35` against three hard bots accepted H1 (a win rate of 35% rather than 30%) after 128 deals, LLR 3.09 against the bound 2.94. Every move of the bot was legal: 0 refused moves in all the matches above, and Catanatron chose no move our rules lack in 18 584 decisions.

The bot is the hard bot plus search: its own moves outside the search and in every playout are the hard bot's. So "how much better than hard" is the value of the search itself. Against Catanatron, for comparison, the hard bot alone won 52% ± 6.9 ([arena.md](arena.md)).

## Speed

One decision of the example game (`catan_mcts --decisions 8 --decision 3`, a main-phase decision with 20 moves, late in the game) on an i7-11800H: 3 700 playouts/s on one thread. A playout from early in the game is a whole game and takes about four times longer, so a decision at 400 playouts takes 0.1–0.5 s of one core, and a whole game of the bot about 14 s of CPU. Matches run one game per thread: 600 games of `mcts:400` take 10 minutes on 16 threads.

Root parallelization on the same decision, 2 000 playouts per worker:

| threads | playouts/s |
|---|---|
| 1 | 3 720 |
| 2 | 7 320 |
| 4 | 13 600 |
| 8 | 18 000 |
| 16 | 23 700 |

Nearly linear up to the 8 physical cores, then the hyper-threads add little, the same shape as `catan_selfplay` ([performance.md](performance.md)).

## Root parallelization and the browser

A search can be split into W independent searches, each with its own seed (its own drawn hands and its own tree), whose root statistics are added up (`merge_searches`). That is root parallelization (Chaslot, Winands and van den Herik 2008): no shared tree, no locks, so threads and Web Workers scale like the independent games of `catan_selfplay`. The cost is that W small trees are shallower than one big one. `catan_mcts --workers W` runs it natively, and `scripts/check.sh` compares a checksum of every visit and win of four decisions between the native build and WebAssembly under Node: equal.

## Limits

- The tree does not model the opponents; their play is the playouts' elites. A person who plays very differently from our pool is predicted badly.
- Offers to players are searched only as the zoo bot proposes them.
- The robber's victim choice and the setup get the same budget as any decision. The setup review ([setup-review.md](setup-review.md)) spends some 28 000 playouts on a settlement; 400 can only nudge the zoo bot's choice.
