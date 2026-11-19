add_executable(catan_tests
  arena_test.cpp
  belief_test.cpp
  bits_test.cpp
  map_elites_test.cpp
  mcts_test.cpp
  topology_test.cpp
  game_test.cpp
  record_test.cpp
  replay_test.cpp
  random_player_test.cpp
  setup_review_test.cpp
  winrate_test.cpp
  zoo_test.cpp)
target_link_libraries(catan_tests PRIVATE catan_record catan_warnings GTest::gtest_main)
# Recorded games made by catan-project's `game_record simulate`.
target_compile_definitions(catan_tests PRIVATE CATAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# gtest_discover_tests runs the binary after it is built and registers every
# TEST() as its own CTest test, so ctest reports and filters them one by one.
include(GoogleTest)
gtest_discover_tests(catan_tests)

# The C++ zoo against the Go bots on the recorded tournament games in data/
# (scripts/check.sh runs the same tool on every record catan-project has).
if(TARGET catan_zoo_check)
  add_test(NAME zoo_matches_go
    COMMAND catan_zoo_check
      ${CMAKE_CURRENT_SOURCE_DIR}/data/zoo-game-00088.json
      ${CMAKE_CURRENT_SOURCE_DIR}/data/zoo-game-00206.json
      ${CMAKE_CURRENT_SOURCE_DIR}/data/hard-game-00030.json)
endif()
