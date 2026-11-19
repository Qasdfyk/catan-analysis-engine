#!/usr/bin/env bash
# Local CI. GitHub Actions is off for this repo, so this script is the gate
# before merging: Debug with sanitizers, Release with the default compiler,
# Release with clang, a short benchmark run to prove they still run, 2000
# random games, zoo games, a short arena match, a tiny MAP-Elites search,
# the validation set and the sanity positions, the Catanatron bridge where Catanatron is installed,
# the difference test against catan-project's Python rules,
# the WebAssembly build (same games, win chances and setup review as native) with
# the web viewer's tests.
#
# Usage: scripts/check.sh                  all checks
#        scripts/check.sh --bench          same, then run the benchmarks for real
#        scripts/check.sh --games N        random games in the difference test (default 20)
#
# The difference test needs catan-project checked out next to this repo with
# its backend venv (../catan-project/backend/.venv). Without it the script
# fails rather than passing silently; CATAN_SKIP_DIFFTEST=1 skips it on purpose.
#
# The WebAssembly step needs Emscripten (emsdk, by default in ~/emsdk; see
# docs/web.md) and Node. Without them it fails too; CATAN_SKIP_WASM=1 skips it.
set -euo pipefail

cd "$(dirname "$0")/.."

bench=false
games=20
while [ $# -gt 0 ]; do
  case "$1" in
    --bench) bench=true ;;
    --games) games="$2"; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

step() { echo; echo "== $*"; }

jobs="$(nproc)"

for preset in debug release clang-release; do
  step "$preset: configure"
  cmake --preset "$preset"
  step "$preset: build"
  cmake --build --preset "$preset" -j"$jobs"
  step "$preset: test"
  ctest --preset "$preset" -j"$jobs"
done

step "benchmarks: smoke run (release)"
# min_time takes a unit suffix since Google Benchmark 1.8: "0.05s" = 50 ms per
# benchmark, "100x" = exactly 100 iterations.
build/release/benchmarks/catan_benchmarks --benchmark_min_time=0.05s

step "random games (release): no refused action"
# Exits 1 if the engine refuses any move of the random player, which only
# picks legal moves.
build/release/tools/catan_selfplay --games 2000

step "zoo games (release): no refused move"
build/release/tools/catan_selfplay --games 2000 --bots zoo --threads "$jobs"

if [ "${CATAN_SKIP_ZOO_RECORDS:-0}" = 1 ]; then
  step "zoo against Go on recorded games: SKIPPED (CATAN_SKIP_ZOO_RECORDS=1)"
else
  # The tests check the C++ zoo against the Go bots on a few records in
  # tests/data; this checks it on every tournament record catan-project has
  # locally (its .logs/zoo-data, from the tournaments of docs/bot-zoo.md).
  zoo_records=../catan-project/.logs/zoo-data
  if compgen -G "$zoo_records/*/*.json.gz" >/dev/null; then
    step "zoo against Go on every record in $zoo_records"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    for f in "$zoo_records"/*/*.json.gz; do
      gzip -dc "$f" > "$tmp/$(basename "$(dirname "$f")")-$(basename "$f" .gz)"
    done
    build/release/tools/catan_zoo_check --show 5 "$tmp"/*.json
  else
    step "zoo against Go: no records in $zoo_records (only tests/data was checked)"
  fi
fi

step "arena (release): a short match, no refused move"
out="$(build/release/tools/catan_arena --candidate hard --opponents zoo --deals 200 --threads "$jobs")"
echo "$out"
echo "$out" | grep -q " 0 unfinished, 0 refused moves" || { echo "arena games went wrong" >&2; exit 1; }

step "MAP-Elites (release): a tiny search, its elites play in the arena"
tmp_elites="$(mktemp)"
build/release/tools/catan_map_elites --generations 2 --batch 16 --games 40 --recheck 40 --reeval 20 --final 100 \
  --seed-bots 8 --threads "$jobs" --out "$tmp_elites" | tail -4
out="$(build/release/tools/catan_arena --candidate elite:0 --opponents elites --elites "$tmp_elites" --deals 64 \
  --threads "$jobs")"
rm -f "$tmp_elites"
echo "$out"
echo "$out" | grep -q " 0 unfinished, 0 refused moves" || { echo "elite games went wrong" >&2; exit 1; }
step "MAP-Elites: data/elites.json loads, the page's map matches it"
build/release/tools/catan_arena --candidate elite:0 --opponents hard --deals 16 --threads "$jobs" | tail -1
python3 scripts/style-map-web.py --check

step "validation set A: reads back, baseline model scores it"
gzip -dc data/set-a.jsonl.gz | build/release/tools/catan_validate eval --model data/baseline.json | tail -1

step "sanity positions C (hard bot, then with the Robber on the leader)"
build/release/tools/catan_validate sanity --bot hard --games 200 --seed 3001 --threads "$jobs"
build/release/tools/catan_validate sanity --bot hard+leader=1 --games 200 --seed 3001 --threads "$jobs"

# Catanatron (GPL-3.0) is not part of this repository; the bridge is checked
# only where it is installed (docs/arena.md says how).
catanatron_python="${CATAN_CATANATRON_PYTHON:-/tmp/catanatron/bin/python}"
if [ -x "$catanatron_python" ] && "$catanatron_python" -c "import catanatron.players.minimax" 2>/dev/null; then
  step "Catanatron bridge: every move it makes is one our rules accept"
  out="$(build/release/tools/catan_arena --candidate catanatron:weighted --opponents hard --no-trades --deals 32 \
    --threads "$jobs" --python "$catanatron_python")"
  echo "$out"
  echo "$out" | grep -q " 0 unfinished, 0 refused moves" || { echo "Catanatron moves were refused" >&2; exit 1; }
  echo "$out" | grep -q ", 0 it chose that our rules have no move for" || { echo "unmapped Catanatron moves" >&2; exit 1; }
else
  step "Catanatron bridge: not installed at $catanatron_python, skipped"
fi

if "$bench"; then
  step "benchmarks: full run (release)"
  build/release/benchmarks/catan_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
fi

if [ "${CATAN_SKIP_DIFFTEST:-0}" = 1 ]; then
  step "difference test: SKIPPED (CATAN_SKIP_DIFFTEST=1)"
else
  python=../catan-project/backend/.venv/bin/python
  if [ ! -x "$python" ]; then
    echo "difference test: $python not found; check out catan-project next to this repo" >&2
    echo "and create its backend venv, or set CATAN_SKIP_DIFFTEST=1" >&2
    exit 1
  fi
  step "difference test against catan-project (python preset)"
  cmake --preset python
  cmake --build --preset python -j"$jobs"
  "$python" tools/difftest.py --games "$games"
fi

if [ "${CATAN_SKIP_WASM:-0}" = 1 ]; then
  step "WebAssembly: SKIPPED (CATAN_SKIP_WASM=1)"
else
  if ! command -v emcc >/dev/null; then
    emsdk_env="${EMSDK:-$HOME/emsdk}/emsdk_env.sh"
    if [ ! -f "$emsdk_env" ]; then
      echo "WebAssembly: emcc not found and no $emsdk_env; install emsdk (docs/web.md)" >&2
      echo "or set CATAN_SKIP_WASM=1" >&2
      exit 1
    fi
    # shellcheck disable=SC1090
    source "$emsdk_env" >/dev/null 2>&1
  fi
  step "WebAssembly: build (wasm preset)"
  cmake --preset wasm
  cmake --build --preset wasm -j"$jobs"
  step "WebAssembly: same games as the native build"
  # Equal checksums mean the module plays exactly the moves of the native
  # engine, not merely that it runs.
  for bots in random zoo; do
    native="$(build/release/tools/catan_selfplay --games 2000 --bots "$bots" | grep checksum)"
    wasm="$(node web/scripts/selfplay-node.mjs --games 2000 --bots "$bots" | grep checksum)"
    echo "$bots, native: $native"
    echo "$bots, wasm:   $wasm"
    [ "$native" = "$wasm" ] || { echo "checksums differ" >&2; exit 1; }
  done
  step "WebAssembly: same win chances as the native build"
  # Win counts of every point of a game, folded into one number.
  native="$(build/release/tools/catan_winrate --playouts 50 --threads "$jobs" tests/data/zoo-game-00088.json | grep 'wins checksum')"
  wasm="$(node web/scripts/winrate-node.mjs --playouts 50 tests/data/zoo-game-00088.json | grep 'wins checksum')"
  echo "native: $native"
  echo "wasm:   $wasm"
  [ "$native" = "$wasm" ] || { echo "win chances differ" >&2; exit 1; }
  step "WebAssembly: same setup review as the native build"
  # A small budget: one thread in Node plays the full one in about a minute.
  native="$(build/release/tools/catan_setup --first 4 --confirm 40 --threads "$jobs" tests/data/zoo-game-00088.json | grep 'setup checksum')"
  wasm="$(node web/scripts/setup-node.mjs --first 4 --confirm 40 tests/data/zoo-game-00088.json | grep 'setup checksum')"
  echo "native: $native"
  echo "wasm:   $wasm"
  [ "$native" = "$wasm" ] || { echo "setup reviews differ" >&2; exit 1; }

  step "WebAssembly: same search as the native build"
  native="$(build/release/tools/catan_mcts --decisions 4 --iterations 30 --workers 2 tests/data/zoo-game-00088.json | grep 'mcts checksum')"
  wasm="$(node web/scripts/mcts-node.mjs --decisions 4 --iterations 30 --workers 2 tests/data/zoo-game-00088.json | grep 'mcts checksum')"
  echo "native: $native"
  echo "wasm:   $wasm"
  [ "$native" = "$wasm" ] || { echo "searches differ" >&2; exit 1; }
  if [ ! -d ../catan-project/frontend/src ]; then
    echo "web viewer: ../catan-project/frontend/src not found; the viewer draws catan-project's board" >&2
    exit 1
  fi
  step "web viewer: install, typecheck, test, build"
  (cd web && npm ci --no-audit --no-fund && npm test && npm run build)
fi

echo
echo "All checks passed."
