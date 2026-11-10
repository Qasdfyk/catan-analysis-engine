# Third-party code, fetched at configure time.
#
# Each dependency is a release tarball pinned by tag *and* by SHA256, so a
# moved tag or a tampered download fails the configure step instead of
# silently changing what we build. To upgrade: change the URL, download the
# tarball, put its `sha256sum` here.
#
# SYSTEM (CMake 3.25+) marks their headers as system headers, so warnings
# inside gtest/benchmark headers do not trip our -Werror in files that
# include them.

include(FetchContent)

if(CATAN_BUILD_TESTS)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM)
  FetchContent_MakeAvailable(googletest)
endif()

if(CATAN_BUILD_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
  # Their own -Werror would make a new compiler release break our build.
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
    URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
    SYSTEM)
  FetchContent_MakeAvailable(benchmark)
endif()
