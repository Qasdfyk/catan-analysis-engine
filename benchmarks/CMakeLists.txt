add_executable(catan_benchmarks
  bits_bench.cpp)
target_link_libraries(catan_benchmarks PRIVATE catan catan_warnings benchmark::benchmark_main)
