# zodiac13

## Build & test

- Requires `VCPKG_ROOT` set (vcpkg manifest mode; deps come from `vcpkg.json`).
- Configure + build (Linux, Ninja generator, already the default): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)`.
- Build a single target (faster iteration): `cmake --build build --target <target>` (e.g. `raylib_module`).
- If running inside PRoot (check `uname -a` for `PRoot-Distro` in the kernel string), cap parallel build jobs at 2-3 (e.g. `-j3`) instead of `-j$(nproc)` — higher counts have hung the session before. `-j3` was verified safe (incremental and full clean rebuilds, no hang) on 2026-09-16.
- `ccache` is auto-detected by `CMakeLists.txt` (`CMAKE_CXX_COMPILER_LAUNCHER`) when installed — install it (`apt install ccache`) to speed up rebuilds.
- Run tests: `ctest --test-dir build` (or run `build/bin/tests/z13_test_runner` directly for gtest filters, e.g. `--gtest_filter=...`).
- `python3 make.py -b` / `-br` wraps the Debug/Release configure+build+install cycle end-to-end (used for full local builds, not incremental iteration).

## Codebase map

- `src/lib_core` — shared flecs/module plumbing used by every module: `core.h`/`core_types.h` (flecs world setup, lifecycle events like `RegisterComponentsEvent`/`InitPhasesEvent`/`InitSystemsEvent`), `module_factory_base.h` (plugin factory interface), plus `log.h`, `math.h`, `flecs_utils.h`, `world_serializer.h`.
- `src/lib_z13/components/include/z13/components` — flecs component structs shared across modules (`building.h`, `gameplay.h`, `input.h`, `bootstrap.h`, `status.h`, `z13.h`) and `constants/constants.h`.
- `src/lib_z13/modules/z13_module` — main gameplay module: `bootstrap/`, `building/` (`building_input_system`, `building_system`), `gameplay/`, `input/` (`gameplay_input_system`, input config loading). Factory in `z13_module_factory.cpp`. Its own tests live under `tests/`, with the `Z13TestWorld` harness in `tests/support/z13_test_world.h`.
- `src/lib_z13/modules/bullet_module` — Bullet physics plugin. `bullet_components.h` exposes only `RigidBody` (component used outside the module); `PhysicsWorld` (the Bullet dynamics-world singleton) is declared in `physics_world.h`/`physics_world.cpp`, private to the module. Factory in `bullet_module_factory.cpp`.
- `src/lib_raylib_module` — rendering/platform/GUI plugin: `render/` (environment, lights, skybox, render resources), `platform/` (SDL), `gui/` (ImGui windows/keybindings), `tools/` (asset loading, Assimp, input publishing, math conversion). Factory in `raylib_module_factory.cpp`.
- `src/lib_z13/z13_launcher` — loads module factories (`.so` plugins) into one flecs world at startup.
- `src/lib_z13/schemas/fbs` — FlatBuffers schemas for world (de)serialization (`world_serializer.h` in `lib_core`).
- `src/tests` — the `z13_test_runner` gtest binary; links module factories directly (see `Z13TestWorld`) rather than loading them as plugins.
- `src/zodiac13` — the actual game executable (`main.cpp`).

Each gameplay/render module (`bullet_module`, `raylib_module`, `z13_module`) is a hot-loadable plugin: a `ModuleFactoryBase` subclass exported via `BOOST_DLL_ALIAS`, whose `RegisterModules` does `world.import<XModule>()`. Systems inside a module register through three `flecs::world` observers keyed on `RegisterComponentsEvent` → `InitPhasesEvent` → `InitSystemsEvent` (see `bullet_module/src/physics_system.cpp` for a compact example of the pattern, including why singleton values are set in the systems-event rather than next to their component registration).

## Flecs components

- Entity state is serialized through flecs meta reflection (reflect-cpp based, see `component_meta.h`), which only walks plain data — so a component must never hold a raw pointer, `unique_ptr`, or `shared_ptr` field.
  - Exception: a singleton component that wraps non-serializable engine/runtime machinery and is always rebuilt rather than saved/loaded (`PhysicsWorld`, `Skybox`, `RenderModel`, `Lighting`) is exempt and may keep a `shared_ptr`-backed pImpl.
- For per-entity state that wraps a native/engine object (a Bullet body, a GPU model, ...): make the component an empty tag, and own the actual object in a side table (`std::unordered_map<flecs::entity_t, T>`) kept by the owning singleton/system. `unordered_map` is node-based, so an element's address stays stable across insert/erase of other entries — no smart pointer needed even though the object itself can't move. See `bullet_module`'s `RigidBody` tag + `PhysicsWorld`'s internal `bodies` table, and `raylib_module`'s `BuildingBlock` tag + `EnvironmentRenderSystem`'s `BlockModels` table, for the pattern.
- Where a stable-address pImpl is still needed (the singleton exemption above), name the backing struct `State` (not `Impl`) and its handle member `state_`; the handle itself must be `shared_ptr`, not `unique_ptr` — flecs can in principle duplicate a component, and `unique_ptr` wouldn't tolerate that.
- State that must survive save/load or a network restore is marked inside the component struct with independent nested properties: `using State = void;` (empty structs become state tags) and, for singletons, `using Singleton = void;` (a nested type doesn't affect reflect-cpp; a C++ base class would break it). Register through `RegisterComponent<T>`/`RegisterComponents<...>` (`lib_core/world_state.h`), which applies the properties; new properties are added the same way. State components live on entities tagged `StateEntity`; a singleton with both properties on its component entity. Unmarked components, and singletons without `State`, are never state. Everything derived from state (native objects, side tables, helper entities) is rebuilt by systems that compare against those components every frame, not by `OnAdd`/`OnRemove`/`OnSet` observers; observers are for external events (input, save/load requests, lifecycle). Register through the helpers, not raw `.member()` chains: module registration runs inside observers, where flecs defers commands and each `.member()` overwrites the previous one (the helpers use `ImmediateScope`). A system that creates/destroys entities other systems read later in the frame declares `.write<...>()`, and one that checks components inside its body (`has<>`) declares `.read<...>()`; otherwise flecs defers the merge to the next frame.
- Access a singleton component (`.add(flecs::Singleton)`) as a query/observer term directly — add it to the system's/observer's type list and take it as a parameter — rather than calling `world.ensure<T>()`/`get_mut<T>()` inside the callback body.

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

- Prefer writing clear, self-documenting code (good names, small functions) over explaining unclear code with comments.
- Avoid long comments/comment blocks, unless the logic being described is genuinely complex — one or two sentences is usually enough. This applies even to comments explaining a non-obvious design decision (e.g. why a type uses a pImpl, why a struct's field order matters) — state the reasoning tersely, don't write a paragraph.
- In implementation code, add a comment mainly when a non-obvious or debatable decision was made, to explain the reasoning for future readers.
- Don't restate reasoning that's already documented elsewhere in this file or the codebase (e.g. a project-wide convention) — reference it briefly instead of re-deriving it at every call site.
- The same applies to commit messages: keep them short, a couple of sentences is enough.
- Never include Claude Code session/conversation IDs or other internal tooling metadata in commit messages.

## Branch size

- A branch's total diff should average no more than 1000 lines of code. Exceeding this is acceptable only in exceptional cases.
- If a branch exceeds this limit, flag it immediately during code review.
