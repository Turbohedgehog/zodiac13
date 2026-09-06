#pragma once

#include <z13_tests/z13_tests_export.h>

// Runs the whole GoogleTest suite bundled in z13_tests.dll.
// Returns the RUN_ALL_TESTS() result (0 == success).
extern "C" Z13_TESTS_EXPORT int z13_run_all_tests(int argc, char** argv);
