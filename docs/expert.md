# The expert level and the golden positions

Step 12b of the plan. The searching bot of [mcts.md](mcts.md) leaves the arena: it plays on the platform as the "expert" level, against people. And a set of positions from real games gets a deep analysis once, frozen as golden positions (set B, next to set A and the sanity positions C of [arena.md](arena.md)) for the move grading of step 13 and for the network later.

Code: [`expert.hpp`](../include/catan/expert.hpp) (a running game rebuilt from its record, and the move), [`tools/expert.cpp`](../tools/expert.cpp) (`catan_expert`, the program the platform runs), [`golden.hpp`](../include/catan/golden.hpp) (the label of a position) and [`tools/golden.cpp`](../tools/golden.cpp) (`catan_golden`, which builds the set). On the platform's side: `gateway/internal/bot/expert.go` in catan-project.

## How a move gets from the game to the engine and back

The platform's bots are Go programs. Each one holds a WebSocket to the gateway and sees the game the way a person does: the board, its own hand, everyone's card counts. That is not enough for the search, which needs what the seat can know about the other hands (the lower bounds of `HandBelief`, [mcts.md](mcts.md)) and the table memory of the hard bot. Both are the history of the game, not its present.

The platform already keeps that history: the record of every running game, written next to the state in one Redis transaction ([game-record.md](https://github.com/Qasdfyk/catan-project/blob/main/docs/game-record.md)). So on its move the Go bot asks for it and passes it on:

```
game (WebSocket) ──view──▶ Go bot ── hard bot: "is it my move?" ── no ──▶ wait
                                         │ yes
                                         ▼
               backend POST /internal/rooms/{id}/record  (bots service only)
                                         │ record so far
                                         ▼
             catan_expert (stdin) {"id", "seat", "record"}
               replay every event, follow what the seat saw,
               ISMCTS on 4 threads until 1 s or 1 600 playouts
             catan_expert (stdout) {"id", "action", "payload"}
                                         │
                                         ▼
                              Go bot sends the move
```

Design choices, and what was rejected:

- **A separate process, not cgo.** One `catan_expert` serves every expert seat of the bots service, answering requests concurrently (a thread per request, each searching on `--threads`). The Go build stays pure Go (`CGO_ENABLED=0`, a static binary), a crash of the engine cannot take the bots service down, and the Go side restarts the engine on the next question. The cost of the pipe is microseconds against a search of up to a second.
- **The record, not the view.** A bot that rebuilt the game from what it sees over the WebSocket would be fair by construction, but it would need its own event tracking, which the record reader already does and the difference test of [difftest.md](difftest.md) already checks. The record holds more than the seat may know (a card stolen between two others, the development cards drawn), so fairness rests on the engine reading only `HandBelief::known[seat]`. Two tests hold it to that: `tests/belief_test.cpp` (no bound is ever above the true hand) and `tests/mcts_test.cpp` (two games that differ only in what the seat cannot see give the same search). The record endpoint is internal (the `X-Internal-Key` of the gateway and the bots), so no player can read it.
- **The hard bot as the gate and the fallback.** The Go hard bot answers first. If it has no move (not the seat's turn, its own offer waiting for answers), the engine is not asked. If the engine fails, times out or disagrees about whose move it is (a record newer than the view), the hard bot's move is played. After a refused move the bot takes the hard bot's safe move, as every level does. An expert seat therefore never stalls a game, and without an engine (`EXPERT_COMMAND` unset) it is simply the hard bot.
- **A time budget, not a playout count.** In the arena the bot searches a fixed number of playouts so a game replays exactly. In a live game it gets `MctsSettings::deadline`: the search stops after at least one playout once the time is up. Measured on 17 decisions along `tests/data/zoo-game-00088.json` (i7-11800H, 4 threads, nothing else running): 1 600 playouts take 0.20–0.25 s in the setup, where every playout is a whole game, 0.10–0.20 s in the middle and 0.03–0.07 s near the end. Rolls and answers to trades are not searched and take microseconds. So on this machine the limit of 1 s only binds when the machine is busy, which is what it is for. The thinking time counts toward the bots' delay between moves, so an expert plays no slower than a hard bot where it does not search.

`table_from_record` keeps the belief, the table memory and the pieces exactly as `play_arena_game` keeps them while it plays, in the same order, so the bot sees the same thing in the arena and on the platform. A test rebuilds every searched decision of two recorded games from cut records and compares the state and the belief with `record_decisions`. The moves go back in the platform's form through `event_from_action`, the inverse of the record reader; a test sends every move of every kind (every corner, side, hex, seat, resource pair, and every move zoo bots make in four games) there and back.

One difference was found on the way: `record_decisions`, which `catan_mcts` and the golden positions use, sees only the states, so its table memory has the settlements but not the offers made in the turn. The hard bot makes at most two offers a turn and looks at that count, so a decision after an offer is searched as if none had been made. `table_from_record` reads the events and has the count; `catan_golden` takes its memory from there.

## Running it

```bash
build/release/tools/catan_expert --record tests/data/zoo-game-00088.json --seat 1
```

prints the move of seat 1 at the end of that record (cut it with `jq` to ask about an earlier moment). On the platform, `scripts/dev.sh` of catan-project finds `build/release/tools/catan_expert` next to it and starts the bots with

```
EXPERT_COMMAND="catan_expert --elites data/elites.json --time-ms 1000 --threads 4"
```

and its Docker image builds the engine from this repository (the `engine` stage of `gateway/Dockerfile`). `go run ./cmd/tournament -levels expert,hard,hard,hard -expert "..."` plays it in tournaments.

## Golden positions (set B)

Set A of [arena.md](arena.md) scores a whole position: who wins from here. Step 13 needs more: how much each move loses against the best one. That needs many more playouts per position than a review can spend, so it is done once, deeply, and frozen. A cheaper method (the move grading, later the network) is then checked against these labels and never trained on them.

```bash
build/release/tools/catan_golden --per-game 2 --threads 14 --out data/golden-b.jsonl records/*.json
```

From each of 600 tournament games of the platform (the 200 hard and 400 zoo games of the zoo tournament of [zoo.md](zoo.md), unpacked as `hard-game-*.json` and `zoo-game-*.json`; the tool reads plain JSON) it takes two decisions the searching bot would search, one from each half of the game, so the set covers the setup, the middle and the end. Each one is labelled the way the setup review grades corners ([setup-review.md](setup-review.md)):

- **Halving.** Every move the bot would consider gets 128 playouts; the better half gets twice as many more, and so on until 3 finalists are left.
- **Confirmation.** The finalists and the move played in the game get 2 400 fresh playouts each. A move's value comes from these alone, so the best move is not inflated by the noise that picked it (the winner's curse of [map-elites.md](map-elites.md)).

A playout draws the hidden cards from what the seat knew (`HandBelief`, [mcts.md](mcts.md)), makes the move and lets the hard bot finish the seat's turn and the elites play the rest. Playout *i* uses the same hidden cards and the same chance for every move. The file stores the true position too, but a reader that grades moves must search with `known` only, as the label did.

The run took 17.6 minutes on 14 threads of an i7-11800H: 20.5 million playouts, 17 100 per position. `scripts/golden-stats.py data/golden-b.jsonl.gz` prints the numbers below.

| | |
|---|---|
| Positions | 1 200: 860 in the main phase, 193 Robber moves, 108 setup, 34 before a roll (a knight or not), 5 Road Building |
| Moves per position | median 7, mean 9.8, max 54 |
| Standard error of the best move's value | median 0.66 pp, max 0.79 pp |
| Move played = best move | 410 of 1 200 (34%) |
| Loss of the move played | mean 1.87 pp; more than 3 pp in 230 positions (19%) |
| Halving value of the best move − its confirmation | +0.80 pp on average |
| Another finalist ahead in the confirmation | 605 positions (50%) |

What this says, and what it does not:

- **The confirmation is needed.** The best move of the halving looks 0.8 pp better than it is. That is the same size as the standard error, and a grade threshold near "best" would be off by it.
- **Finalists are mostly too close to tell apart.** In half the positions another finalist comes out ahead in the confirmation. With a standard error of 0.66 pp on each value, two moves within about 2 pp are not separated by 2 400 playouts. Step 13 has to mark such grades as within the noise rather than call one of them a mistake.
- **These are games between bots**, and the values are chances to win against the elites, not against people. The players of these games lose 1.9 pp a decision on average against the best move the playouts find; how much a person loses is a question for human set H ([plan.md](plan.md)).
- The labels hold for this zoo and these elites. If the playout bots change, the set has to be labelled again, and the old labels say how far the change moved them.
