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
- String constants: declare as `constexpr std::string_view`, not `const char*`. When a C API (raylib/SDL/ImGui/flecs/...) needs a null-terminated `const char*`, call `.data()` on a `string_view` that's known to span a whole string literal (safe: the literal's own `\0` is the next byte) — never on a `string_view` that could be a substring or come from arbitrary input. For a `string_view` *parameter* (any caller, not just literals), materialize an owning `std::string` first and pass `.c_str()`. Fixed third-party callback signatures (e.g. `raylib::TraceLogCallback`) keep `const char*` as-is — they can't be changed.
- Avoid raw pointers where possible; prefer references, smart pointers (`std::unique_ptr`/`std::shared_ptr`), or non-owning views instead.
- Avoid static variables.
- Avoid exceptions for error handling; prefer `std::expected` instead.
- Default member initializers: use brace-init (`int x {};`, `bool y {};`) instead of `= 0`/`= false`; for Eigen members use `Type::Zero()` instead of `{0.f, 0.f}`.
- Don't expose `void*` or raw-pointer-plus-count pairs in APIs. Forward-declare the concrete type instead of erasing it to `void*`, and return a standard container/view (e.g. `const std::vector<T>&`, `std::span<T>`) instead of a pointer-and-length out-parameter.
- Group related scalar fields that travel together (e.g. width/height, x/y) into a single `Eigen::Vector2i`/`Eigen::Vector2f` rather than separate members.
- Mark intentional `switch`/`case` fallthrough with `[[fallthrough]];`.
- Prefer `std::format` over `snprintf`/manual char buffers or string concatenation (`+`) for building strings.
- Represent filesystem paths as named `std::filesystem::path` constants, not bare string literals passed inline.
- Avoid magic numbers; give them a named `constexpr` constant.
- Always use braces for `if`/loop bodies, even single-statement ones.

## Comments

- Avoid long comments/comment blocks, unless the logic being described is genuinely complex — one or two sentences is usually enough.
- In implementation code, add a comment mainly when a non-obvious or debatable decision was made, to explain the reasoning for future readers.
- The same applies to commit messages: keep them short, a couple of sentences is enough.

## Branch size

- A branch's total diff should average no more than 1000 lines of code. Exceeding this is acceptable only in exceptional cases.
- If a branch exceeds this limit, flag it immediately during code review.
