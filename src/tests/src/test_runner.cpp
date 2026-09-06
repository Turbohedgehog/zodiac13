#include <z13_tests/run_tests.h>

#include <gtest/gtest.h>

extern "C" Z13_TESTS_EXPORT int z13_run_all_tests(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
