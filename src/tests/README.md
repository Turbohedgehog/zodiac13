# z13_tests

Test subproject. All test code compiles into `bin/tests/z13_tests.dll`; a thin
`z13_test_runner.exe` loads it and runs the suite (driven by CTest).

Here the world serializer in `lib_core` (`<lib_core/world_serializer.h>`,
`<lib_core/component_meta.h>`) is exercised — flecs world-state round-trip plus an
opt-in benchmark measuring binary (msgpack) vs JSON serialization.

## Run

```
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The benchmark tests are `DISABLED_` (measure in a Release build):

```
z13_test_runner --gtest_also_run_disabled_tests --gtest_filter=*BinaryVsJson*
```
