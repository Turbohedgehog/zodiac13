# Building inside PRoot

Development runs on ARM under PRoot (Termux); `uname -a` shows `PRoot-Distro` in the kernel string.

## Job caps

- `-j$(nproc)` has hung the whole session; `-j3` was verified safe for incremental and full clean rebuilds on 2026-09-16. `make.py` applies the cap itself (`-j` overrides it).
- A full vcpkg rebuild (after a triplet change, a new port, a baseline bump) ignores `-jN` for ports. Only `-j1` with `VCPKG_MAX_CONCURRENCY=1` completed without hanging. Launch it detached (`setsid -f bash -c '...' > log 2>&1 < /dev/null`) and read the log: a long foreground or background tool call is killed together with the build.
- GitHub downloads during configure drop intermittently: retry the configure.

## Timings to expect (Debug, ARM, -j3)

- An edit to a header most objects depend on: about an hour (check with perf-measure's `header_width.py`).
- An edit to one module's source: minutes.
- `ctest --test-dir build`: about 10-15 minutes for ~700 tests.

## Killing builds

Never `pkill -f` a pattern that also appears in your own command line — it kills the calling shell. Use `scripts/stop_build.py`, which stops the process group `build.py` recorded.
