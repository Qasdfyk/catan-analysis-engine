# Plan

The goal is a post-game review of a Catan game, like the game review on chess.com: a chance-to-win curve, the luck split from the decisions, and every decision graded against the best move, judged only on what the player could see. The review looks at finished games only. It gives no advice during a game.

Step numbers are fixed once given, so the other docs can point at them. New steps get a letter (13a) instead of moving the old ones.

Status: **done**, **next**, **later**, or **conditional** (depends on someone else's answer).

## Done

| Step | What | Notes |
|---|---|---|
| 1–3 | Game records in catan-project: a versioned JSON format, every finished game saved, a playtest | [game-record.md](https://github.com/Qasdfyk/catan-project/blob/main/docs/game-record.md) in catan-project |
| 4–6 | The rules engine in C++20: board as indices and bitmasks, the full rules with trading between players | [rules-engine.md](rules-engine.md) |
| 7 | The difference test against catan-project's Python rules | [difftest.md](difftest.md) |
| 8 | Speed against the Python engines, a `perf` profile | [performance.md](performance.md) |
| 9 | WebAssembly build and the replay viewer on GitHub Pages | [web.md](web.md) |
| 9b | A zoo of bot styles on the platform | [zoo.md](zoo.md) |
| 10, 10a | Win chances by Monte Carlo, the luck split, calibration; the zoo ported to C++ | [win-chances.md](win-chances.md), [zoo.md](zoo.md) |
| 10b | The arena with SPRT, the Catanatron bridge, validation set A, sanity positions C | [arena.md](arena.md) |
| 10c | MAP-Elites over the zoo | [map-elites.md](map-elites.md) |
| 11 | Setup review: every corner of the initial placement graded | [setup-review.md](setup-review.md) |
| 12a | The ISMCTS bot | [mcts.md](mcts.md) |

## Stage A: a demo of the full post-game flow

| Step | Status | What |
|---|---|---|
| 12b | next | The ISMCTS bot as an "expert" level on catan-project, and golden positions B: a few positions analysed deeply once, frozen as a test set. |
| 13a | later | The post-game flow on catan-project: the game ends, the player is asked "Review this game?", and the viewer opens with that game's record. First as a button on the end screen; the viewer takes a record from outside the page. |
| 13b | later | A start page for players rather than developers: "dice or decisions?" first, with an example game, its chance curve, the luck split and the setup heat map. Engine speed stays on the Performance tab. |
| 13c | later | A converter interface that does not depend on any platform: an adapter from a foreign record format to the engine's `Replay`. Designed and tested only on catan-project records. |

The demo is ready when 13a works, 13b shows the example game, and the whole flow fits in a short screen recording.

## Stage B: the review itself

This does not depend on Colonist (below) and runs alongside stage A.

| Step | Status | What |
|---|---|---|
| H | later | Human set H: games of people on catan-project, played with their consent, player names removed. Used to measure (calibration, setup review, move grades, the network), never to train. It will be small, tens of games rather than thousands, so its numbers will have wide intervals. |
| 13 | later | The move review: each decision graded by the drop in win chances against the best move, judged on what the player knew, with the best move and a short reason. Grade thresholds are set on bot games first and checked on set H. |
| 14 | later | Self-play data from a league (the network, its older versions, the zoo); a policy and value network trained in PyTorch. |
| 15 | later | Running the network in the browser: ONNX Runtime Web against our own inference in C++. |
| 16 | later | One network with a policy and a value head; the network guides the search and the search gives it better targets. A new checkpoint must win an SPRT match against the last one and must not be worse on sets A, B, C and H. |

Training data is synthetic only: self-play, the zoo, MAP-Elites, the league. No game of a person is used to train.

## Stage C: Colonist

| Step | Status | What |
|---|---|---|
| 17 | conditional | Review of games played on [Colonist](https://colonist.io), only with the company's written permission. |
| 18 | later | Trial: ask a language model about a graded mistake, given the engine's structured result. Dropped if too heavy. |

### Why the Colonist step is conditional

Colonist's [Terms](https://colonist.io/terms) (last updated October 6, 2026) forbid, without the company's written consent:

- using content or data from the service to train or improve AI or machine learning models;
- scraping, crawling or indexing any part of the service, including game replays and statistics, by automated or manual means;
- collecting data from the service to build a separate database, service or product;
- using its internal APIs, private endpoints or data structures other than through the standard interface.

A tool that reads a finished game from Colonist and reviews it elsewhere collects game replays for a separate product. So nothing in this repo reads, converts or tests on Colonist games, and the converter of 13c is built on catan-project records only. We will ask Colonist for permission. What step 17 becomes depends on the answer:

- Permission for personal or research use: Colonist's rule options in the engine, a converter and an extension for our own games, calibration on them. Nothing published.
- Permission for a public tool, or an official export of finished games: a public extension that shows the review after a game, never during it.
- An offer to work together: whatever form it takes.
- No answer, or a refusal: nothing connects to Colonist. The product is catan-project with the review built in.

This is our reading of the Terms, not legal advice.
