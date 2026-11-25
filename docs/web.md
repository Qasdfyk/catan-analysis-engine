# The engine in the browser

The web viewer (`web/`) opens a game record and steps through it move by move, on the same board as the game itself. The rules that rebuild each position are the C++ engine, compiled to WebAssembly with Emscripten, so the page needs no server: it is a static site, and a record never leaves the computer it is opened on. Later steps put the win chances and the move grades on the same page.

## Building

Emscripten is not a system package here. It lives in `~/emsdk` and is pinned:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.13 && ~/emsdk/emsdk activate 6.0.13
source ~/emsdk/emsdk_env.sh           # puts emcc on PATH and sets EMSDK
```

Then, from the repo root:

```sh
cmake --preset wasm                   # Emscripten's toolchain file, Release
cmake --build --preset wasm -j"$(nproc)"
cd web && npm ci && npm run dev       # http://localhost:5173
```

The `wasm` preset builds only the module: the tests, benchmarks and command-line tools need a real OS (threads, files) and stay native. The module is written to `web/src/wasm/` (gitignored), where Vite imports it as `@engine`.

`scripts/check.sh` builds the module, checks that it plays the same 2000 random games as the native build (equal checksums), and runs the viewer's tests and production build. `scripts/deploy-pages.sh` publishes the built page to the `gh-pages` branch for GitHub Pages.

## The boundary between JavaScript and C++

```
record text ──► Replay (C++, wasm/bindings.cpp) ──► ReplayLog: one Game per event
                         │
page asks for state i ◄──┘  stateJson(i): JSON string ──► JSON.parse ──► SVG
```

- **Strings of JSON, not objects.** Every call returns a JSON string the page parses. Embind can expose C++ structs field by field, but each field read would be a call across the boundary; one `JSON.parse` of a few kilobytes is cheaper and keeps `bindings.cpp` small. The JSON is the same `snapshot()` the difference test compares against Python, plus the bank and the dice.
- **Every state kept.** `ReplayLog` (`include/catan/replay.hpp`) applies all events once and keeps every `Game`. A `Game` has no heap inside, so a game of 1300 events is about 1 MB of states, and jumping to any move is an array lookup instead of a replay from the start. `ReplayLog` is plain C++, so the native tests cover it (`tests/replay_test.cpp`).
- **No exceptions across the boundary.** Reading a record can throw (bad JSON, a broken header). `ReplayLog::from_text` catches everything and returns the message in `error()`; the page shows it. Exceptions inside the module use WebAssembly's own exception handling (`-fwasm-exceptions`), which every current browser and Node support, instead of Emscripten's older emulation in JavaScript.
- **Manual memory.** A `Replay` lives in the module's heap, which JavaScript's garbage collector does not see. The page calls `delete()` on the old one when a new record is opened.
- **Positions as indices.** The engine speaks in indices (corner 0..53); the page turns them back into the (hex, direction) spelling of game records with `topologyJson()` (`web/src/view.ts`).

The module is about 210 KB of WebAssembly (80 KB gzipped), most of it nlohmann/json and the C++ library.

## The board is catan-project's

The viewer does not draw its own board. It imports catan-project's `Board` component (`frontend/src/components/Board.tsx`, with its terrain art and `board.css`) from the checkout next to this repo, through the Vite alias `@platform`, and feeds it a `GameView` built from the engine's state (`web/src/view.ts`). The marks of the last move (`builtPieces`) and the tiles that produced on a roll (`producingTiles`) come from catan-project too. So a game looks the same in the game and in its review, and a change to the board's look reaches both.

What this costs:
- Building the viewer needs catan-project checked out next to this repo, as the difference test already does. The published page has no such link: Vite copies everything it needs into `web/dist`.
- The page is React (`web/src/App.tsx`), one component per tab in `web/src/pages/`: Start, Replay and Engine. The page is a demo for a first visit, so it shows the strongest few results and leaves the rest to `docs/`; the old `#/performance` and `#/how-it-works` links land on Engine. Under the replay the page shows the chance to win (also a timeline to click), the review of the initial placement, and how much of each player's swing came from dice. Tabs are hash routes (`#/replay`), because GitHub Pages serves a single file and cannot route `/replay` back to it. Charts are hand-written SVG and divs (`web/src/components/Charts.tsx`), with no chart library. The numbers the page quotes from `docs/` live in one file, `web/src/facts.ts`. Vite's `dedupe` makes catan-project's files use the viewer's React, since two copies on one page break React.
- `web/src/view.ts` fills the parts of `GameView` the board reads; the rest (log, timers) gets neutral values. A new field the board starts to read shows up as a type error in `npm run build`, because `tsc` checks catan-project's files too.

## Review this game

When a game on catan-project ends, the game-over dialog offers "Review this game?". The button opens the viewer in a new tab with that game, without a file to download and upload. The two pages are on different origins (the game on localhost or its own server, the viewer on GitHub Pages), so they talk by `window.postMessage`, which works across origins between a page and the window it opened:

```
game page:  window.open(viewer + "#/replay?from=opener")   (not noopener: the viewer needs window.opener)
viewer   -> game page: {type: "catan-review:ready", version: 1}
game page -> viewer:   {type: "catan-review:record", version: 1, record, label, seat}
                    or {type: "catan-review:error", version: 1, message}
```

- **The viewer speaks first.** A message posted to a window whose page has not loaded yet is lost, so the game page waits for `ready` before it sends. It answers every `ready` for half an hour, so a reload of the viewer gets the game again.
- **Who may talk to whom.** The game page sends the record only to the viewer's origin (`postMessage(..., origin)`), so if the tab navigates elsewhere the browser drops it, and it listens only to the window it opened. The viewer takes records only from its own opener, but from any origin: a browser extension or another platform can use the same door. A record is data to look at, checked like any file (`web/src/adapters.ts`).
- **The record never leaves the computer.** It goes from tab to tab, not over the network or into a URL. The cost: a review cannot be shared as a link.
- **The review is the asking player's.** `seat` says who clicked. The viewer draws that player's chance on top with the others faded, puts them first in the luck split, lists only their settlements in the setup review (the others on request) and reviews those first. The others stay as context: a player's chance moves with everyone's dice and moves. "Review for" switches to another seat; a record opened from a file starts with its first person.
- `#/replay?from=opener` without an opener (a bookmark, a reload after the game tab closed) says so and shows the example game.

Code: `web/src/handoff.ts` here, `frontend/src/net/review.ts` in catan-project, whose end-to-end test plays a game and checks that a stand-in viewer receives its record. `VITE_REVIEW_URL` points the game at a local viewer (`npm run dev -- --port 5174`).

## The start page

The start page is for players: it replays the example game with Alice's review following along (the chance curve, the luck split, the setup heat map while the settlements go down), and the moves that lost the runner-up the most, each with the best move and why. The review is computed ahead of time by `node web/scripts/review-example.mjs` (about 40 s on 15 threads) into `web/public/example-review.json`, 13 KB gzipped, so the page shows it at once, also on a phone. The numbers are the ones Replay computes live for the same game: the same module, seeds and playouts per point; `web/src/review.test.ts` plays a sample of points again and compares. Rerun the script after any change to the engine, the bots or the elites that changes playouts.

## Speed

The benchmark on the Engine tab and `node web/scripts/selfplay-node.mjs` play the same random games as `catan_selfplay` (same seed, same checksum) on one thread. For 5 000, 20 000 and 100 000 games the tab knows the native checksums (`NATIVE_CHECKSUMS` in `facts.ts`) and says whether the browser matched. Parallel work in the browser (win chances, the setup review) runs on several Web Workers, each a separate copy of the module. WebAssembly threads proper are left out on purpose: they need `SharedArrayBuffer`, which a browser only allows on pages served with cross-origin isolation headers, and GitHub Pages cannot set headers. The move review runs one decision at a time per worker instead ([move-review.md](move-review.md#on-the-page)).

```sh
build/release/tools/catan_selfplay --games 20000
node web/scripts/selfplay-node.mjs --games 20000
```

## Not done yet

- Hidden information: the viewer shows everything (stolen cards, development cards), which is right for reviewing a finished game. The move grades judge a move only by what the player knew ([move-review.md](move-review.md)), but the board still shows every hand.
- Records from other sites: the converter interface ([converters.md](converters.md)) has one adapter, catan-project's. Colonist records need the company's written permission first (step 17, see [plan.md](plan.md#why-the-colonist-step-is-conditional)).
