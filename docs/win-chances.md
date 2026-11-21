# Chances to win

Step 10 of the plan: each player's chance to win at every turn of a finished
game, the chart of it in the web viewer, and how much of each player's swing
came from the dice and how much from decisions. Code:
[`winrate.hpp`](../include/catan/winrate.hpp), the tool
[`tools/winrate.cpp`](../tools/winrate.cpp), the page side
[`web/src/chances.ts`](../web/src/chances.ts).

## Method

Monte Carlo. From a position, zoo bots ([zoo.md](zoo.md)) play the game to
the end many times; a seat's chance is the share of those playouts it wins.

- **Kept** from the position: board, buildings, robber, every hand and held
  development card, points, awards, whose turn it is. This is a spectator's
  view; a player's own view (hidden cards drawn from what they could know)
  comes with the move review, step 13.
- **Drawn** anew in every playout: dice, stolen cards, the order of the
  undrawn development cards, and the players. In the web viewer every seat is
  a new random zoo bot in every playout, since the page cannot know who played.
  For the platform's tournament games the tool can seat the bot that actually
  played (`--bots record`).
- **Streams.** Playout *i* draws from its own random stream (seed, *i*),
  whatever the position, thread or chunk. Playouts can then be cut into chunks
  for threads or Web Workers and added up to exactly one run's result.
  `scripts/check.sh` checks that the WebAssembly build gives the same win
  counts as the native one.

### Where along the game

Every turn after the setup has two points: just before its roll and just
after (the roll and the cards it paid). The game adds a point before the first
settlement and one at the end, which is about 140 points for a game of 70 turns.

- **Luck** for a seat = the sum of the changes of its chance across the rolls.
- **Decisions** = every other change: the setup, building, trades, the robber
  after a 7, development cards, and the moves of the other players too.
- Per seat, start + luck + decisions = end (1 for the winner, 0 for the rest),
  exactly.

The split puts the robber after a 7 under decisions, since where it goes is a
choice. The card it steals is random and rides along with that decision.

### Noise

Each point is an estimate with its own error. The split adds up some 140
differences of such estimates, so their errors add up too. With 400 playouts
per point, a seat's luck over a whole game moves by 10 to 20 percentage points
from one seed to another; the error falls as 1/√playouts.

**What did not help: common random numbers.** We tried fixing the dice of each
turn by (playout, turn), so playout *i* before a roll and playout *i* after it
roll the same numbers in every later turn. The spread did not change (spread
of the luck of each seat over 8 seeds, 400 playouts per point, before → after:
example game 15.4 → 20.8, 15.1 → 15.4, 8.1 → 11.4 pp; zoo game 11.4 → 11.3,
12.7 → 11.6, 10.0 → 7.7 pp). After a different roll the bots hold different
cards and make different moves, so the two futures part ways within a turn or
two whatever the later dice. We took it out.

A precise split needs a value function cheap enough to average over all 36
rolls at every turn. GNU Backgammon measures luck the same way, with its
network. The value network of steps 14 to 16 is for that. Until then the page
shows the split with 95% intervals. These come from 10 passes over the game
that use disjoint playouts, so each pass is an independent estimate (batch
means), and the page calls the split a rough guide.

## Calibration

When the estimate says 30%, does that player win about 30% of the time? The
test uses the platform's tournament records ([zoo.md](zoo.md)): 400 zoo games
and 200 games of four hard bots. It takes 4 random points just before a roll
from each game and 200 playouts per point, and compares every seat's estimate
with who won. The positions of one game share its winner, so they are not
independent. The 95% intervals therefore come from a bootstrap over games
(1000 resamples), not over positions; resampling positions would make the
intervals too narrow.

```
build/release/tools/catan_winrate --calibrate 4 --playouts 200 --threads 16 --bots record records/*.json
```

Zoo games, playouts by the bots that played (`--bots record`):

| said | mean | won | 95% interval | count |
|---|---|---|---|---|
| 0–10% | 3.1% | 4.7% | 3.4–6.1% | 2311 |
| 10–20% | 14.5% | 15.8% | 13.1–18.6% | 1114 |
| 20–30% | 24.5% | 22.9% | 19.8–25.7% | 849 |
| 30–40% | 34.4% | 37.0% | 33.1–40.8% | 613 |
| 40–50% | 44.4% | 42.1% | 37.1–46.9% | 447 |
| 50–60% | 54.6% | 54.4% | 47.8–60.8% | 344 |
| 60–70% | 64.4% | 61.3% | 54.7–68.6% | 243 |
| 70–80% | 74.5% | 68.3% | 59.7–77.2% | 183 |
| 80–90% | 83.9% | 71.7% | 61.9–81.0% | 138 |
| 90–100% | 96.0% | 92.4% | 86.6–97.1% | 158 |

Brier score 0.136 (95% interval 0.128–0.146). Always saying 25% scores 0.1875.

| games | playout bots | Brier (95%) | buckets outside their interval |
|---|---|---|---|
| zoo, 400 | recorded | 0.136 (0.128–0.146) | 0–10%, 80–90% |
| zoo, 400 | random zoo | 0.140 (0.132–0.148) | 80–90%, 90–100% |
| hard, 200 | recorded (hard) | 0.144 (0.133–0.153) | none |
| hard, 200 | random zoo | 0.146 (0.135–0.156) | none |

What it says:

- **Mostly calibrated, but too sure at the top.** In the zoo games, positions
  rated 80–90% were won about 72% of the time. The cause is not noise: with
  1000 playouts per point the result is the same (84.4% said, 73.5% won,
  interval 64.1–82.6%). At the bottom it is a little too sure the other way,
  saying 3% for seats that won 5%. The likely cause is that the recorded games
  ran on the real-time platform, which is noisier than our playouts. Moves
  arrive late or twice, and a bot whose move was refused falls back to a safe
  move (10a found this when matching the C++ zoo to the Go bots). A leader
  there loses more often than in a clean simulation. Checking this needs
  records of native zoo self-play, where the playouts and the game are the
  same process. The frozen validation set of step 10b is the place for it.
- **The players matter less than expected here.** Hard games played out by
  random zoo bots score as well as by the hard bots that played them, within
  the intervals. 200 games cannot tell them apart; the intervals are about
  ±10 points in the upper buckets.
- **For people it will be worse.** Every game here was played by bots of the
  same family as the playouts. A human plays differently from any zoo bot, and
  that gap is what the calibration on human set H (games of people on
  catan-project, see [plan.md](plan.md)) will measure.

## Speed

Example game of the web viewer, 142 points, 400 playouts each, i7-11800H:

| build | threads | playouts/s |
|---|---|---|
| native, GCC 15 -O3 | 1 | 5 690 |
| native | 16 | 26 900 |
| WebAssembly in Node | 1 | 5 060 |
| WebAssembly in Chromium (headless) | 8 workers | 12 400–14 600 |

The tool cuts the work into chunks of 50 playouts that threads take from a
shared counter, so no thread waits for a long position to end. The page does
the same with jobs of 200 playouts per point, handed to whichever worker is
free. The page runs 10 passes of 200 playouts per point, 2000 in all, about
280 000 playouts for a whole game. The first pass draws the whole curve in a
couple of seconds; the rest sharpen it.
