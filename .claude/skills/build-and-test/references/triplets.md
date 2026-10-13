# Shared libraries and plugin state

- `triplets/` overrides the built-in `x64-linux`/`arm64-linux` so flecs, spdlog and fmt are shared libraries there, as on `x64-windows`. `lib_core` is itself shared (`z13_core`, next to the executable). Every plugin must see the same flecs and spdlog process state and link one copy instead of its own.
- A dependency with process-wide state that plugins share must be shared too. CI's `.github/scripts/check_duplicated_state.py` fails on data defined in more than one binary; known constants are allowed there, each with a reason.
  - Typical failures: a non-constexpr global (a `std::filesystem::path` constant) in a static library linked into several plugins, or a constexpr constant in an anonymous namespace of such a library. Put constants in headers as `inline constexpr`.
- `z13_plugin_smoke` (ctest `PluginSmoke`) loads the server plugins the launcher's way to check this at run time.
- Editing a triplet changes every port's ABI hash, i.e. rebuilds all dependencies (references/proot.md for the procedure); per-port build flags go in an overlay port (`ports/`).
- CI builds `arm64-linux` natively on `ubuntu-24.04-arm`. 32-bit ARM (`arm-linux-gnueabihf`) is not supported: vcpkg marks `raylib` as `!arm32`.
