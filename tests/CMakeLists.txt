add_executable(catan_tests
  bits_test.cpp)
target_link_libraries(catan_tests PRIVATE catan catan_warnings GTest::gtest_main)

# gtest_discover_tests runs the binary after it is built and registers every
# TEST() as its own CTest test, so ctest reports and filters them one by one.
include(GoogleTest)
gtest_discover_tests(catan_tests)
