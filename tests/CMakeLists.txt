add_executable(catan_tests
  bits_test.cpp
  topology_test.cpp
  game_test.cpp
  record_test.cpp)
target_link_libraries(catan_tests PRIVATE catan_record catan_warnings GTest::gtest_main)
# Recorded games made by catan-project's `game_record simulate`.
target_compile_definitions(catan_tests PRIVATE CATAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# gtest_discover_tests runs the binary after it is built and registers every
# TEST() as its own CTest test, so ctest reports and filters them one by one.
include(GoogleTest)
gtest_discover_tests(catan_tests)
