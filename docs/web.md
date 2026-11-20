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

## Speed

The benchmark on the Engine tab and `node web/scripts/selfplay-node.mjs` play the same random games as `catan_selfplay` (same seed, same checksum) on one thread. For 5 000, 20 000 and 100 000 games the tab knows the native checksums (`NATIVE_CHECKSUMS` in `facts.ts`) and says whether the browser matched. Parallel work in the browser (win chances, the setup review) runs on several Web Workers, each a separate copy of the module. WebAssembly threads proper are left out on purpose: they need `SharedArrayBuffer`, which a browser only allows on pages served with cross-origin isolation headers, and GitHub Pages cannot set headers. The review will run one game at a time per worker instead.

```sh
build/release/tools/catan_selfplay --games 20000
node web/scripts/selfplay-node.mjs --games 20000
```

## Not done yet

- Hidden information: the viewer shows everything (stolen cards, development cards), which is right for reviewing a finished game. A "seen by player X" view comes with the move grades, which must judge a move only by what the player knew.
- Records from other sites: a platform-neutral converter interface is step 13c, built and tested on catan-project records only. Colonist records need the company's written permission first (step 17, see [plan.md](plan.md#why-the-colonist-step-is-conditional)).
