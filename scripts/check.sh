#!/usr/bin/env bash
# Local CI. GitHub Actions is off for this repo, so this script is the gate
# before merging: Debug with sanitizers, Release with the default compiler,
# Release with clang, a short benchmark run to prove they still run, and the
# difference test against catan-project's Python rules.
#
# Usage: scripts/check.sh                  all checks
#        scripts/check.sh --bench          same, then run the benchmarks for real
#        scripts/check.sh --games N        random games in the difference test (default 20)
#
# The difference test needs catan-project checked out next to this repo with
# its backend venv (../catan-project/backend/.venv). Without it the script
# fails rather than passing silently; CATAN_SKIP_DIFFTEST=1 skips it on purpose.
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

echo
echo "All checks passed."
