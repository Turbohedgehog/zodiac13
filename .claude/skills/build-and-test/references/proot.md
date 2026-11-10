# Building inside PRoot

Development runs on ARM under PRoot (Termux); `uname -a` shows `PRoot-Distro` in the kernel string.

## Job caps

- `-j$(nproc)` has hung the whole session; `-j3` was verified safe for incremental and full clean rebuilds on 2026-09-16. `make.py` applies the cap itself (`-j` overrides it).
- A full vcpkg rebuild (after a triplet change, a new port, a baseline bump) ignores `-jN` for ports. Only `-j1` with `VCPKG_MAX_CONCURRENCY=1` completed without hanging. Launch it detached (`setsid -f bash -c '...' > log 2>&1 < /dev/null`) and read the log: a long foreground or background tool call is killed together with the build.
- GitHub downloads during configure drop intermittently: retry the configure.
- 2026-11-06, measured: `-j4` and `-j5` aborted `cmake`/`ninja` within minutes (`ninja: fatal: waitpid: Function not implemented`, libuv `uv__io_poll: Assertion 'errno == EINTR' failed`, then `cannot make pipe for command substitution`); `VCPKG_MAX_CONCURRENCY=2` ran a 107-minute vcpkg build and `-j3` the whole project (~50 minutes) without a failure.

## System packages the first configure needs

vcpkg ports ask the system for these; install them before the first configure (`sudo apt install ...`):

- `autoconf autoconf-archive automake libtool` (`pthread-stubs`, the X11 ports; `autoconf-archive` is not in a default Debian).
- With the renderer: `libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev pkg-config` (`glfw3`, `raylib`, and SDL3's X11 backend).

A port built before its packages are installed is not rebuilt afterwards: vcpkg's ABI hash ignores system packages, so the port counts as up to date. SDL3 configured without X11 has `SDL_X11`, `SDL_WAYLAND` and `SDL_OPENGL` off (see `buildtrees/sdl3/config-arm64-linux-out.log`), and the game stops with `[sdl] SDL_Init: No available video device`. Rebuild it: from a directory without `vcpkg.json`, `vcpkg remove sdl3:arm64-linux --recurse --x-install-root=<repo>/vcpkg_installed`, then configure with `VCPKG_BINARY_SOURCES=clear`.

## Plugin that will not load

`cannot load '.../raylib_module.so': Bad file descriptor` is a stale `errno`; the cause is in `LD_DEBUG=files bin/zodiac13 2>&1 | grep -i error`. It was `undefined symbol: glfwGetProcAddress`: the desktop raylib port needs `glfw3`, its exported target does not link it, and a shared module links with undefined symbols (`nm -D <module>.so | grep ' U '` finds them before a run). `raylib_module` links `glfw` itself outside Android.

## Claude Code freezes

In PRoot a `stat`/`open` costs 150-300 µs (about 100 times native; `mprotect` and `getpid` stay cheap; a failing `close()` measured 90 µs), and Claude Code's UI stalls for seconds to a minute (`claude --debug`, `~/.claude/debug/<session>.txt`, `[event-loop-stall]`: little CPU used, startup takes ~2 minutes, ~4 stalls a minute, input is not accepted meanwhile). Native Termux does not run it: since 2.1.113 the binary is built for glibc (`unexpected e_type: 2`).

Measured 2026-11-10, in this order:

- Not the cause: disk (604 MB/s, `git status` 0.2 s), network (IPv4 0.12 s; IPv6 to api.anthropic.com times out, but `--dns-result-order=ipv4first` changed nothing), memory, CPU or scheduler, `--debug`, `--no-kill-on-exit`, a `fork()` of a 400 MB process (10 ms). The "git took 5-9 s" lines in the debug log are the stalled loop itself.
- Sampling `/proc/<claude pid>`: the main thread sits in state `D`, `wchan=kernel_clone`, ~60 of 66 s. A `/proc` poll every few ms shows why: every ~8 s Claude Code clones itself (its own `git -c core.askPass= ...`, `git -c core.fsmonitor= ...`, `ps -A -o pid= ...`; not from settings, hooks or plugins), the parent waits in `kernel_clone` until the child execs, and the child spends 7-8 s in `ptrace_stop` before the exec.
- Suspected mechanism, not proven: the child walks the descriptors up to `ulimit -n` (32768 here, ~3 s at 90 µs a syscall) between fork and exec.
- Workaround: start it with a low limit, `claude() { ( ulimit -n 1024; command claude "$@" ); }` in `~/.bashrc` (only claude gets it; builds keep 32768). The user reported it "seems better"; clone-to-exec under an active turn was not measured afterwards. If the stalls come back, rerun the poll (children of the claude pid, log exe/argv changes) and compare with 7-8 s.

## Timings to expect (Debug, ARM, -j3)

- An edit to a header most objects depend on: about an hour (check with perf-measure's `header_width.py`).
- An edit to one module's source: minutes.
- `ctest --test-dir build`: about 16 minutes serial (`-j1`, 2026-11-07) for ~1500 tests in 282 CTest entries.

## Killing builds

Never `pkill -f` a pattern that also appears in your own command line — it kills the calling shell. Use `scripts/stop_build.py`, which stops the process group `build.py` recorded.
