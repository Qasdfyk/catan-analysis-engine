# Running benchmarks

Microbenchmarks measure nanoseconds, so small disturbances on the machine change the result more than the code under test does. This page is how to get numbers worth comparing on a laptop or under WSL.

## Build type

Always measure a Release build (`release` or `clang-release` preset). A Debug build has no optimisation, and the `debug` preset also has sanitizers, which slow every memory access. Google Benchmark prints `***WARNING*** Library was built as DEBUG` when its own library is a Debug build; if you see it, the numbers mean nothing.

```sh
cmake --preset release
cmake --build --preset release -j"$(nproc)"
build/release/benchmarks/catan_benchmarks
```

## Run each benchmark several times

One run is one sample. Repeat and look at the spread:

```sh
build/release/benchmarks/catan_benchmarks \
  --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

This prints the mean, median, standard deviation and coefficient of variation (`cv`, the standard deviation as a percentage of the mean) of the five runs. Compare medians. If `cv` is more than a few percent, the machine was busy and the comparison is not reliable: close other programs and run again.

Useful flags:

- `--benchmark_filter=REGEX`: run only matching benchmarks, e.g. `--benchmark_filter=Touching`.
- `--benchmark_min_time=0.5s`: run each benchmark at least this long (the `s` suffix is required; `100x` means exactly 100 iterations instead).
- `--benchmark_format=json --benchmark_out=results.json`: save results to compare later with `tools/compare.py` from the Google Benchmark repository.

## CPU frequency scaling

Laptop CPUs change their clock speed all the time: they boost when cool and idle, and slow down when hot or on battery. A benchmark that runs right after another one can see a different clock. Google Benchmark prints `***WARNING*** CPU scaling is enabled` when it can detect this; under WSL it usually cannot, so the absence of the warning does not mean the clock is fixed.

What helps:

- Plug the laptop in and use the high-performance power plan.
- Let the machine idle for a moment before measuring; do not compile in the background.
- Use repetitions and medians, as above, and repeat the whole run once to see whether the order of results is stable.

## Pinning to one core

The scheduler can move the process between cores mid-run, and the cores of a modern laptop differ (performance and efficiency cores). Pinning to one core removes that source of noise:

```sh
taskset -c 2 build/release/benchmarks/catan_benchmarks --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true
```

Under WSL the core numbers are virtual, so pinning helps less than on bare Linux, but it still keeps the process on one virtual CPU.

## Compare like with like

- Compare numbers only from the same machine, the same build preset and the same session. A result from another computer, another compiler or another day is a different experiment.
- Compiler matters: the `release` (GCC) and `clang-release` presets can give different numbers for the same code. That difference is itself worth looking at.
- The ratio between two benchmarks run together is more trustworthy than either absolute number.
- The build targets generic x86-64, without `-march=native`, so the compiler uses only instructions that every x86-64 CPU has. Keep that in mind before comparing with numbers from a build that sets `-march`.
