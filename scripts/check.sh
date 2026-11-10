#!/usr/bin/env bash
# Local CI. GitHub Actions is off for this repo, so this script is the gate
# before merging: Debug with sanitizers, Release with the default compiler,
# Release with clang, then a short benchmark run to prove they still run.
#
# Usage: scripts/check.sh          all checks, benchmarks only smoke-tested
#        scripts/check.sh --bench  same, then run the benchmarks for real
set -euo pipefail

cd "$(dirname "$0")/.."

bench=false
for arg in "$@"; do
  case "$arg" in
    --bench) bench=true ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
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

echo
echo "All checks passed."
