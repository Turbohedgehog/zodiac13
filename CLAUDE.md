# zodiac13

## Build & test

- Requires `VCPKG_ROOT` set (vcpkg manifest mode; deps come from `vcpkg.json`).
- Configure + build (Linux, Ninja generator, already the default): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)`.
- Build a single target (faster iteration): `cmake --build build --target <target>` (e.g. `raylib_module`).
- `ccache` is auto-detected by `CMakeLists.txt` (`CMAKE_CXX_COMPILER_LAUNCHER`) when installed — install it (`apt install ccache`) to speed up rebuilds.
- Run tests: `ctest --test-dir build` (or run `build/bin/tests/z13_test_runner` directly for gtest filters, e.g. `--gtest_filter=...`).
- `python3 make.py -b` / `-br` wraps the Debug/Release configure+build+install cycle end-to-end (used for full local builds, not incremental iteration).

## Code style

- C++ code must follow the Google C++ Style Guide (https://google.github.io/styleguide/cppguide.html).
- Prefer idiomatic modern C++ over C-style code (e.g. `std::array`/`std::vector` over raw arrays, `std::string_view` over `char*`, RAII over manual resource management).
- Avoid raw pointers where possible; prefer references, smart pointers (`std::unique_ptr`/`std::shared_ptr`), or non-owning views instead.
- Avoid static variables.
- Avoid exceptions for error handling; prefer `std::expected` instead.

## Comments

- Avoid long comments/comment blocks, unless the logic being described is genuinely complex — one or two sentences is usually enough.
- In implementation code, add a comment mainly when a non-obvious or debatable decision was made, to explain the reasoning for future readers.
- The same applies to commit messages: keep them short, a couple of sentences is enough.

## Branch size

- A branch's total diff should average no more than 1000 lines of code. Exceeding this is acceptable only in exceptional cases.
- If a branch exceeds this limit, flag it immediately during code review.
