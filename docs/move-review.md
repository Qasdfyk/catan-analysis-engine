# Move review: every decision graded

Step 13 of the plan. Each decision of a player in a finished game gets a grade from **best** to **blunder**, by how much of their chance to win it gave away against the best move, judged only on what that player could see, and a short "why". Code: [`review.hpp`](../include/catan/review.hpp), [`review.cpp`](../src/review.cpp), the tool [`tools/review.cpp`](../tools/review.cpp) (`catan_review`) and the tests in [`tests/review_test.cpp`](../tests/review_test.cpp). On the page (release 4): `web/src/grades.ts`, `web/src/why.ts` and `web/src/components/MoveReview.tsx`, see [On the page](#on-the-page).

## How one decision is graded

A review does for every decision what the golden positions of [expert.md](expert.md) do for 1 200 of them, with about a sixth of the playouts:

1. **What the player knew.** The record is walked once, event by event, with the same bookkeeping as the arena and the expert (`apply_record_event` in `expert.hpp`): the table memory, and `HandBelief`, the cards each seat holds for sure as far as this player can tell ([mcts.md](mcts.md)). Every playout draws the hidden cards from that belief (`determinize`). The true hands in the record never reach a playout, so a move is not punished for a card the player could not see. A test replaces every hidden card by another draw that agrees with what the player knew and checks that the review does not change by a single playout.
2. **The candidates.** The moves the searching bot would consider ([mcts.md](mcts.md), `search_candidates`), plus the move played if the bot would not have considered it (an offer).
3. **Successive halving.** Every candidate gets 32 playouts, the better half 64 more, and so on until 2 finalists are left. The move played rides along every round, because it is the one being graded.
4. **Confirmation.** The 2 finalists and the move played get 800 fresh playouts each. The values come from these alone, so the best move is not inflated by the noise that picked it (the winner's curse of [map-elites.md](map-elites.md)).

A playout makes the move, lets the hard bot finish the player's turn and MAP-Elites bots play everyone else to the end. About 2 800 playouts per decision, 0.12 s on 12 threads of an i7-11800H; a seat of a game has some 40 graded decisions, so one player's review takes about 5 s natively.

**Which decisions.** The turn (build, buy, play a card, trade with the bank, end the turn, an offer the bot would make), the robber and the victim, wherever there is more than one move. Answering offers and discarding are left out, as in the search. The setup is left out by default (`--setup` adds it): the setup review of [setup-review.md](setup-review.md) grades it with ten times the playouts.

## Loss, error and grade

    loss = confirmed value of the best move − confirmed value of the move played

in chance to win (0.05 = 5 percentage points). Its error comes from **pairs of playouts**: playout *i* of every move draws the same hidden cards and the same dice, so each playout gives a pair (won with the best move?, won with the move played?), and the error is the standard error of the mean difference of the pairs. On 3 400 graded decisions of hard bots the median paired error is 1.62 pp against 2.04 pp if the two values were treated as independent: the pairs are positively correlated, because two moves of the same position often lead to similar games. That is the same as 1.6 times more playouts for free. (The setup review still uses the independent error, which errs on the safe side.)

| grade | when |
|---|---|
| best | the move played is the one the halving ranked first |
| good | loss below 3 pp, **or within the noise**: less than 1.96 errors above zero |
| inaccuracy | 3 to 6 pp |
| mistake | 6 to 12 pp |
| blunder | 12 pp or more |

Thresholds in percentage points of chance to win, not of a score like chess engines use: the same 5 points matter in any position. They are smaller than the usual chess ones (lichess marks a blunder at 15 points of winning chances out of 50), because a fair four-player start is 25%, not 50%. Why these values is below; they are to be checked on games of people (set H of [plan.md](plan.md)).

## Checked against the golden positions

`catan_review --golden` grades the 300 first non-setup positions of set B the way a review would and compares with their deep labels (17 000 playouts per position):

| | |
|---|---|
| loss of the review − deep loss | −0.43 pp on average, 1.23 pp mean absolute |
| error of a loss | 1.45 pp paired, 1.84 pp independent |
| same grade | 65% |

| deep \ review | best | good | inaccuracy | mistake | blunder |
|---|---:|---:|---:|---:|---:|
| best | 73 | 33 | 3 | 0 | 0 |
| good | 35 | 97 | 5 | 0 | 0 |
| inaccuracy | 3 | 14 | 7 | 2 | 0 |
| mistake | 1 | 3 | 2 | 9 | 1 |
| blunder | 0 | 0 | 0 | 4 | 8 |

- **Best and good are one grade in practice.** Most disagreements are between them: when two finalists are within 2 pp, which one the halving ranks first is a coin toss at either depth. The page shows them together.
- **False alarms are rare.** Of 246 decisions the deep label calls best or good, the review marks 8 (3.3%) as inaccuracy and none as a mistake or a blunder. With none in 246, a false blunder is below 1.2% of such decisions (95% bound), so fewer than 0.5 a game of some 40 decisions; the plan asks for fewer than 1.
- **Big mistakes are found.** All 12 deep blunders come out as mistake or blunder. The inaccuracies are the hard part: 3 to 6 pp is only twice the error of the review.

**A bug found on the way.** A golden position is stored as a board and a snapshot, and the snapshot lists only the development cards left in the deck. Read back, the deck started at card 0, as if nobody had drawn any, so the hidden cards were dealt from a deck with cards nobody had: opponents got victory point cards that did not exist and won the playouts too early. In 23 of 400 positions the best move's value was off by more than 10 pp (0.32 instead of 0.92 in the worst). `game_from_snapshot` now puts the drawn cards back in front of the deck (the full deck minus what is left), and `Golden.AStoredPositionComesBackTheSame` checks that a stored position plays the same playouts as the one in the game; it fails without the fix. Reviews of game records never went through snapshots and are unchanged. The numbers above and below are after the fix; before it, the same grade came out in 60% and one false blunder showed up.

## Planted blunders

`catan_review --golden --plant` replaces the move played in each golden position with the move the deep halving ranked last, and grades it. 400 positions (in 41 of them every move is the worst, so they are skipped):

| deep \ review | best | good | inaccuracy | mistake | blunder |
|---|---:|---:|---:|---:|---:|
| good | 35 | 157 | 9 | 3 | 0 |
| inaccuracy | 5 | 36 | 25 | 11 | 0 |
| mistake | 1 | 4 | 11 | 49 | 4 |
| blunder | 0 | 2 | 1 | 5 | 42 |

Of 50 planted moves the deep label calls blunders, the review marks 47 (94%) a mistake or a blunder; the plan's target is 90%.

The deep loss of a planted move needs care. "The move the halving ranked last" is the minimum of many noisy estimates, from as few as 128 playouts each, so its halving value is too low: a move that drew bad luck in its playouts is more likely to come out last (the loser's curse, the mirror image of the winner's curse of [map-elites.md](map-elites.md)). Taken as the deep value, it made the review look lenient, by 2.7 pp on average, and only 82% of the planted blunders looked caught. So the deep value of a planted move comes from 2 400 fresh playouts on a seed of its own (`fresh_planted` in `tools/review.cpp`, with `play_move` from `golden.hpp`), as the confirmation does for the best move; the review's losses then come out 0.5 pp below the deep ones on average.

## The thresholds, and the hard bot

`catan_review` on every seat of 30 games between four hard bots (4 992 decisions, 14 million playouts, 9.7 minutes on 12 threads):

| | |
|---|---|
| graded best | 32% |
| best or good | 84% |
| mean loss | 2.06 pp a decision |
| loss of the moves not graded best | median 0.4 pp, 90th percentile 8.1 pp, 99th 30.6 pp |
| blunders per player and game | 1.65 (at 12 pp); 2.17 at 10 pp, 1.17 at 15 pp, 0.71 at 20 pp |

The blunders of the hard bot are real more often than not. They sit in decided positions (the best move's chance is 55% at the median blunder, against 25% at the start): the end of a game, where one move wins or loses it. The most common are a road, an offer, a trade with the bank or a settlement where something else wins; in one game a seat at 8 points played a knight with a year of plenty in hand that, by 800 playouts each, would have won it 94% of the time instead of 23%. The hard bot follows rules and has no idea of "this wins now", which is exactly what the searching bot ([mcts.md](mcts.md)) adds.

So 12 pp stays the blunder threshold: the golden positions put the false alarms at about 0.15 a game, well under the plan's 1, and moving it to 15 or 20 would hide decisions that lose a game. Inaccuracy at 3 pp is the lowest the noise allows (twice the median error).

## Why

A grade says how much a move cost, not what was wrong with it. `explain()` in `review.hpp` makes both moves on the true position (the player's own moves need nothing hidden) and returns the first fact that tells them apart, for an inaccuracy or worse; `web/src/why.ts` puts it into one or two sentences. In order:

| kind | when | says, for example |
|---|---|---|
| wins_now | the best move wins this turn in at least half its playouts, the move played in a quarter fewer | "A city on 6·8·5 wins the game this turn in 100% of the playouts" |
| robber | both move the robber | the pips each hex takes from the leader, the opponents and the player |
| victim | both steal | points and cards of the two victims |
| longest_road, largest_army | the best move takes the award, the move played does not | |
| points | the best move scores more now | |
| production | the best move builds where it collects more pips | the numbers around the corner |
| port | the best settlement is on a port of a kind the player has none of | |
| enables | the best move is a trade or a card, after which the player can pay for something the move played cannot | "you can pay for a city" |
| road_spot | the best road reaches a corner to settle on | the corner's numbers |
| road_length | the best road makes the player's road longer | the holder of the Longest Road |
| ended_turn | the move played ends the turn, the best one builds or buys | |
| dev_card | the best move buys a development card | how many victory points and knights the player had not seen |
| later | none of the above | "the difference shows up later in the playouts" |

A reason states what the moves do, never more: the grade comes from the playouts alone, and a template must not claim what they did not measure. When the pips say the robber was placed well, the text says so and that the playouts favour the other hex for what follows. Words, not the facts, are left to the page, so the same facts can read differently there later.

On the 10 first games of the 4× hard tournament (1 803 decisions, 282 inaccuracies or worse): later 29%, road_length 20%, dev_card 18%, enables 12%, ended_turn 6%, robber 6%, road_spot 4%, wins_now 2%, the rest 2%. The first version, without enables, road_spot, road_length, dev_card and port, said "later" for 81%.

## On the page

The Replay tab grades every decision of the player under review in Web Workers (`grades.ts`, `grades-worker.ts`), each worker one decision at a time with catan_review's budget and seed (`reviewDecisionsJson` and `reviewMove` in `wasm/bindings.cpp`), so the move list fills in from the first turn: 40 decisions of the example game in about 14 s on 8 workers of an i7-11800H, while the chance curve and the setup review run alongside. What the page shows:

- a mark in the move list (✓ for best and good together, ?! inaccuracy, ? mistake, ?? blunder) and on the chance curve for inaccuracies and worse;
- at a graded move, a card with the move played, the best move, both chances with the loss and its 95% interval, and "why"; the best move is drawn dashed on the board;
- the count of grades and the five moves that lost the most, to jump to.

A player's grades are kept while the page stays on the game, so switching "Review for" and back does not start over. `scripts/check.sh` compares the module with the native build on a whole seat of a game (`web/scripts/review-node.mjs`, the same "review checksum" as `catan_review`), "why" included.

The start page shows the example game's grades computed ahead (`web/scripts/review-example.mjs`). They are not Alice's, who had lost the game early and made few decisions after the setup, but Boole's, the bot that lost by a point: the more interesting review to show.

No accuracy score yet: whether the mean loss measures how well someone plays is the open question of the next step.

## What this does not tell

- **The values are chances against elites,** played out by the hard bot for the player. A move that suits a human plan the hard bot would not follow is valued as the hard bot would play it on. That is the main reason for the network of steps 14-16.
- **One move at a time.** A turn is several decisions, and each is graded as if the hard bot played the rest of the turn. A plan of two moves whose first move looks bad on its own is graded on the first move alone.
- **Trades between players** are graded only as the offer the player made. Answering offers is not graded yet.
- **Games of bots.** How people's grades look is a question for set H.

## Running it

```bash
build/release/tools/catan_review --seat 0 --out review.jsonl game.json
build/release/tools/catan_review --threads 16 records/*.json
zcat data/golden-b.jsonl.gz > golden-b.jsonl
build/release/tools/catan_review --golden golden-b.jsonl --limit 300
build/release/tools/catan_review --golden golden-b.jsonl --plant --limit 400
node web/scripts/review-node.mjs --seat 0 tests/data/zoo-game-00088.json   # the same in WebAssembly
```

The summary groups players by the first word of their name in the record ("hard 2" is "hard"), so a tournament of catan-project with `-levels easy,medium,hard,expert -records DIR` gives one row per level. `--out` writes one line of JSON per decision: the move played and the best one, both values, the loss with its error, the grade, whether it is within the noise, and "why". The summary starts with a "review checksum" of every review, which `scripts/check.sh` compares with the WebAssembly module.
