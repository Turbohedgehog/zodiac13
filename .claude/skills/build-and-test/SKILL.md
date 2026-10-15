---
name: build-and-test
description: Builds zodiac13 and runs its tests, including the detached build-and-wait workflow PRoot needs, stopping a stale build and the CI checks to run before a push. Use when asked to build, rebuild, run the tests or ctest, check how a running build is going, or when a change is ready to be verified ("собери", "пересобери", "прогони тесты", "запусти ctest", "какой статус", "проверь, что собирается").
---

# Build and test

Safety rules (job caps in PRoot, `pkill -f`, vcpkg concurrency) are in CLAUDE.md, "Build & test"; this skill is how to work within them.

## Commands

- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` (needs `VCPKG_ROOT`; Ninja is the default generator).
- Build everything: `cmake --build build -j3` in PRoot, `-j$(nproc)` elsewhere. One target: `--target <name>` (`z13_test_runner`, `raylib_module`, ...).
- Tests: `ctest --test-dir build --parallel 3` in PRoot (the scripts pass it; `EnetTransportTest`, on real sockets with wall-clock bounds, runs alone via `RUN_SERIAL`); a gtest filter: `bin/tests/z13_test_runner --gtest_filter='Suite.*'`.
- Whole cycle: `python3 make.py -b` (Debug: `build/`, `bin/`, installs to `install/`) / `-br` (Release: `build-release/`, `bin-release/`, no install), `--bench` adds the benchmarks, `--profile` the Tracy zones; run what was built with `python3 z13.py` (`--game`, `--server [PORT]`, `--tests [--filter]`, `--bench [--filter]`, `--release` for the Release build). The output directory is the CMake cache variable `Z13_BIN_DIR`.
- `ccache` is picked up automatically when installed (`apt install ccache`).

## Detached builds (PRoot, or any build longer than a few minutes)

A foreground build hung the session before, and a tool call over ~10 minutes is moved to the background anyway. The scripts below are Python and work on Linux and Windows; logs go to `<temp>/z13-build` unless `--log-dir` says otherwise. Always:

1. Start it detached: `python3 .claude/skills/build-and-test/scripts/build.py [--tests] [--keep-going] [--target T] [--config C] [--build-dir build-release]`. It picks `-j3` in PRoot and there builds without the renderer (`Z13_BUILD_RENDER=OFF`: no `raylib_module`, CI builds it; `--render` puts it back, reconfiguring only when the option changes, with `VCPKG_MAX_CONCURRENCY=1`), writes `build.log` (and `ctest.log` with `--tests`, run only if the build succeeds) ending in an `EXIT <code>` line, and the PID for `stop_build.py`. `--config` is for multi-config generators (Visual Studio).
2. Wait in a background tool call: `python3 .claude/skills/build-and-test/scripts/wait_build.py` (run_in_background). It prints the unique compiler errors, or the test summary and the failed tests.
3. For "какой статус?": `python3 .claude/skills/build-and-test/scripts/wait_build.py --status` prints `[n/N]`, the errors so far and how many tests ran, without waiting. Answer with the step count, errors, and a time estimate from the pace of the last check.

## One build per change set

- Finish every edit, the review and the comment trimming (finish-branch skill) before starting the build: a build that touches a wide header takes about an hour in PRoot, and the user objected to running it twice.
- If new edits make a running build stale, stop it rather than let it finish: `python3 .claude/skills/build-and-test/scripts/stop_build.py` (stops the whole process group build.py recorded, never by matching command lines).
- A refactor that touches many files (header splits, renames): build with `--keep-going` (`ninja -k 0`) so all errors arrive in one pass, fix them together, then one incremental build with `--tests`.
- Comment-only edits after a green run need no rebuild.
- While iterating, `build.py --target <module>` finds compile errors without the whole tree, and `bin/tests/z13_test_runner --gtest_filter=...` runs just the branch's tests; the test runner itself links every module, so it rebuilds nearly everything once a widely included header (schemas' `*_generated.h`, `building_tuning_generated.h`) changes. Before a push: one full build and all tests.

## Before a push

Run CI's two local checks:

- `python3 .github/scripts/check_listed_sources.py` — every `.cpp`/`.fbs` listed in its `CMakeLists.txt`.
- `python3 .github/scripts/check_duplicated_state.py bin/zodiac13 bin/libz13_core.so bin/modules/*/*.so` (on Windows the `.exe` and `.dll` files) — data defined in more than one binary, see references/triplets.md.

## References

- references/proot.md — why the caps exist, the full vcpkg rebuild procedure, flaky GitHub downloads.
- references/triplets.md — shared flecs/spdlog/z13_core, what `PluginSmoke` and the duplicated-state check guard.
