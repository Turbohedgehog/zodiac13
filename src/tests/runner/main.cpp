#include <z13_tests/run_tests.h>

// Thin host for the test DLL: CTest runs this, all test code lives in z13_tests.
int main(int argc, char** argv) {
  return z13_run_all_tests(argc, argv);
}
