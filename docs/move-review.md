# Move review: every decision graded

Step 13 of the plan, the engine half. Each decision of a player in a finished game gets a grade from **best** to **blunder**, by how much of their chance to win it gave away against the best move, judged only on what that player could see. The grades are not on the web page yet; that is the next step. Code: [`review.hpp`](../include/catan/review.hpp), [`review.cpp`](../src/review.cpp), the tool [`tools/review.cpp`](../tools/review.cpp) (`catan_review`) and the tests in [`tests/review_test.cpp`](../tests/review_test.cpp).

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
| loss of the review − deep loss | −0.31 pp on average, 1.43 pp mean absolute |
| error of a loss | 1.41 pp paired, 1.81 pp independent |
| same grade | 60% |

| deep \ review | best | good | inaccuracy | mistake | blunder |
|---|---:|---:|---:|---:|---:|
| best | 59 | 46 | 3 | 0 | 1 |
| good | 36 | 96 | 4 | 1 | 0 |
| inaccuracy | 3 | 9 | 10 | 4 | 0 |
| mistake | 1 | 4 | 3 | 5 | 3 |
| blunder | 0 | 0 | 0 | 3 | 9 |

- **Best and good are one grade in practice.** Most disagreements are between them: when two finalists are within 2 pp, which one the halving ranks first is a coin toss at either depth. The page should show them together.
- **False alarms are rare.** Of 246 decisions the deep label calls best or good, the review marks 9 (3.7%) as inaccuracy or worse and 1 as a blunder. A seat has some 40 graded decisions a game, so that is about 0.15 false blunders per game. The plan asks for fewer than 1.
- **Big mistakes are found.** All 12 deep blunders come out as mistake or blunder. The inaccuracies are the hard part: 3 to 6 pp is only twice the error of the review.

## Planted blunders

`catan_review --golden --plant` replaces the move played in each golden position with the move the deep halving ranked last, and grades it. 400 positions (in 65 of them every move is the worst, so they are skipped):

| deep \ review | best | good | inaccuracy | mistake | blunder |
|---|---:|---:|---:|---:|---:|
| good | 49 | 119 | 15 | 11 | 0 |
| inaccuracy | 9 | 23 | 7 | 2 | 1 |
| mistake | 4 | 32 | 19 | 24 | 3 |
| blunder | 3 | 9 | 3 | 23 | 44 |

Of 82 planted moves the deep label calls blunders, the review marks 67 (82%) a mistake or a blunder. The plan's target was 90%, but this measurement is biased against the review: "the worst move by the halving" is the minimum of many noisy estimates, from as few as 128 playouts, so it looks worse than it is (the loser's curse, the mirror image of the winner's curse). The review values that move with 800 fresh playouts, and its losses come out 2.7 pp lower than the label's on average. A fair version needs the planted moves valued by fresh playouts too; until then, 82% is a lower bound.

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
```

The summary groups players by the first word of their name in the record ("hard 2" is "hard"), so a tournament of catan-project with `-levels easy,medium,hard,expert -records DIR` gives one row per level. `--out` writes one line of JSON per decision: the move played and the best one, both values, the loss with its error, the grade and whether it is within the noise.
