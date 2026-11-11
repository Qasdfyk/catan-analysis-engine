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

# JSON for reading game records (catan_record only; the rules do not use it).
# The release tarball is the slim one meant for FetchContent.
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  SYSTEM)
FetchContent_MakeAvailable(nlohmann_json)

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

if(CATAN_BUILD_PYTHON)
  # Builds the Python module for the interpreter in Python_EXECUTABLE (the
  # catan-project backend's venv, so the module and game.py meet in one process).
  find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
  FetchContent_Declare(pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v3.0.1.tar.gz
    URL_HASH SHA256=741633da746b7c738bb71f1854f957b9da660bcd2dce68d71949037f0969d0ca
    SYSTEM)
  FetchContent_MakeAvailable(pybind11)
endif()
