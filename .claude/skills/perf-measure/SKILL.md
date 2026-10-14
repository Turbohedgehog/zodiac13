---
name: perf-measure
description: Measures zodiac13's speed and build cost the way that gives trustworthy numbers on the PRoot dev box — which benchmark covers what, comparing variants within one run, per-step timers, prototyping a new algorithm in a benchmark before wiring it in, and how many objects a header edit rebuilds. Use when asked to measure, benchmark, profile, find why something is slow, choose between implementations by speed, or check how wide a header is ("замерь", "бенчмарк", "почему медленно", "сравни по скорости", "сколько стоит", "насколько широкий заголовок").
---

# Measuring

## Benchmarks

They are disabled gtest tests under `*/tests/bench/`, built only with `Z13_BUILD_BENCHMARKS` (`make.py --bench`). Run one with
`bin/tests/z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='RoomEditBench.*'` or `python3 z13.py --bench --filter '...'`.
What each covers: references/benchmarks.md. A new long operation gets a benchmark that prints its phases and counts, not only a total.

## Getting numbers you can trust

- The PRoot box drifts 1.5-2x between runs (thermal, other load): the same code measured 5.2-7.6 s on consecutive runs. Compare variants **inside one run** of one binary (both in the same benchmark), or as shares of a total measured in the same run. Never compare against a figure from an earlier session.
- Debug is the working mode (user decision 2026-10-12), but generic library code (Boost, ranges) is many times slower in Debug than Release: a decision between implementations states both, Debug and Release (below).
- To find where time goes, look at the operation's Tracy zones (below), adding one per step where they are missing, and count how often each step runs (calls, candidates, rejections and why) with temporary prints removed before committing. Counting the rejections usually shows the filter worth adding first.

## Release

`python3 make.py -br --bench` builds Release into `build-release/` and `bin-release/`, beside Debug's `build/` and `bin/` (Release installs nothing); `python3 z13.py --release --bench --filter '...'` runs the benchmarks there. In PRoot a full Release build takes over an hour: start it detached overnight (`build.py --build-dir build-release` once make.py has configured it).

## Tracy

Zones stay in the code, so a measurement doesn't start by placing timers: `Z13_PROFILE_ZONE("Name")` at the top of a function or loop body (a string literal), `Z13_PROFILE_FRAME()` once per host frame (`Core::Update`), both from `lib_profile/profile.h`. They compile to nothing unless the build has `--profile` (`Z13_PROFILE`); then each zone costs a check while no viewer is connected (on-demand mode). A long operation gets a zone on itself and on each of its phases.

- Release with zones: `python3 make.py -br --bench --profile`.
- The viewer and `tracy-capture` must be the client's version (vcpkg `tracy` 0.13.1): the Windows viewer from Tracy's GitHub releases connects to the box over the network (port 8086; in PRoot, the phone's address). Start it first, then the benchmark or server; zones before the connection are not recorded.
- `TRACY_NO_EXIT=1` in the environment keeps a finished benchmark alive until the viewer has its data.
- Without a viewer (PRoot, phone), `tracy-capture -o FILE.tracy -a ADDRESS` writes a capture to open later; build it from Tracy's sources of the same version.
- Counters (calls, candidates, rejections) are still temporary prints, or Tracy plots if they stay.

## A new algorithm on a hot path

Prototype it **inside a benchmark first**, next to the current one, checking both give the same result, and wire it in only after the numbers say so (the opening search went through five in-place rewrites before this). Measure the cases that matter to the player, not only the full rebuild: for the station that is the edit (`RoomEditBench`), since the full build runs once per world.

## Recording results

Write numbers into the plan the work belongs to (plan-and-debt skill) with their conditions: scene, Debug/Release, ARM/PRoot or Windows, what was compared in one run.

## Build cost of a header

`python3 .claude/skills/perf-measure/scripts/header_width.py [HEADER_SUFFIX...]` reads `ninja -t deps` from `build/` and prints how many object files depend on each header (the top 40 without arguments) — the number an edit to that header rebuilds. Measure before and after splitting a header. It needs the Ninja generator (Linux, the default here); a Visual Studio build keeps no dependency log.
