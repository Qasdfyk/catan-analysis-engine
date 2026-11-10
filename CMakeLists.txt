cmake_minimum_required(VERSION 3.25)

project(catan_engine
  VERSION 0.1.0
  DESCRIPTION "Post-game review engine for Catan-style board games"
  LANGUAGES CXX)

# Benchmarks and simulations are meaningless in Debug, so an unconfigured
# build gets Release. Multi-config generators (Visual Studio, Xcode) pick the
# configuration at build time instead, so only touch single-config ones.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# C++20 exactly, without GNU extensions, so gcc and clang accept the same code.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json lets clangd and other editors see the real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CATAN_BUILD_TESTS "Build the unit tests" ON)
option(CATAN_BUILD_BENCHMARKS "Build the benchmarks" ON)
option(CATAN_SANITIZE "Build everything with AddressSanitizer and UBSan" OFF)

# Sanitizers must cover every object file, third-party ones included, so they
# are global options set before the dependencies are fetched.
if(CATAN_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Our warnings live on an INTERFACE target that only our own targets link, so
# GoogleTest and Google Benchmark are never compiled with our -Werror.
add_library(catan_warnings INTERFACE)
target_compile_options(catan_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)

add_library(catan STATIC
  src/bits.cpp)
target_include_directories(catan PUBLIC
  ${PROJECT_SOURCE_DIR}/include)
target_compile_features(catan PUBLIC cxx_std_20)
target_link_libraries(catan PRIVATE catan_warnings)

include(cmake/Dependencies.cmake)

if(CATAN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
