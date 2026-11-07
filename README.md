# catan-analysis-engine

Post-game review for Catan-style board games, in the spirit of a chess game review: step through a finished game, see a grade for every decision, the better move when there was one, and how each player's chances of winning changed, with the swings from dice kept apart from the swings from decisions.

Work in progress. Nothing here runs yet.

## Plan

1. Game records exported from [catan-project](https://github.com/Qasdfyk/catan-project), the browser game this engine is tested against.
2. A rules engine in C++20, checked move by move against the Python rules of catan-project, fast enough to play many thousands of games a second.
3. A WebAssembly build, so the review runs in the browser and the site can be static.
4. Win chances from simulated games, then a search bot, then a small neural network trained by self-play.
5. Move grades from the drop in win chances against the best move, judged only on what the player could see at the time.

The engine only looks at finished games. It gives no advice during a game.

## License

MIT. Catan is a trademark of its owner; this project is not affiliated with it.
