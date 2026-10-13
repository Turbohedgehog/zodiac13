# zodiac13

## Build & test

How to build, test and wait on builds is in the build-and-test skill; the rules:

- Requires `VCPKG_ROOT` set (vcpkg manifest mode; deps come from `vcpkg.json`). Configure + build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`; tests: `ctest --test-dir build`.
- Inside PRoot (`uname -a` shows `PRoot-Distro`): cap parallel jobs at `-j3` — higher counts have hung the session. A full vcpkg rebuild only with `-j1` and `VCPKG_MAX_CONCURRENCY=1`, launched detached. Long builds run detached with a log, never as a long foreground call.
- Never `pkill -f` a pattern that also appears in your own command line — it kills the calling shell.
- Sources and schemas are listed explicitly in each `CMakeLists.txt` (no globs): add a new `.cpp`/`.fbs` there; CI's `.github/scripts/check_listed_sources.py` fails on an unlisted one.
- flecs, spdlog, fmt, Tracy's client (overlay `ports/tracy`) and `lib_core` (`z13_core`) are shared libraries (`triplets/`) so every plugin sees one process state; a dependency with process-wide state that plugins share must be shared too (CI's `check_duplicated_state.py`, ctest `PluginSmoke`). Editing a triplet rebuilds every dependency; per-port flags go in an overlay port (`ports/`).

## Codebase map

- `src/lib_profile` — `z13_profile`, header-only: `Z13_PROFILE_ZONE`/`Z13_PROFILE_FRAME` (`lib_profile/profile.h`), Tracy zones compiled in only with `Z13_PROFILE`; usage in the perf-measure skill.
- `src/lib_core` — flecs/module plumbing used by every module (a shared library, `z13_core`), headers under `include/lib_core/<group>/`, sources mirrored in `src/<group>/`: `world/` (`core.h`/`core_types.h` flecs world setup, `lifecycle.h` world-creation stages, `phase_order.h`, `module_factory_base.h` plugin factory interface), `time/` (simulation clock, tick pacer), `state/` (state markers, snapshots and their codec, rollback, save/load), `settings/` (`config.h`, core settings), `utils/` (`log.h`, `math.h`, `pose_smoothing.h`, `drawn_poses.h`, `flecs_utils.h`, `endpoint.h`).
- `src/lib_z13/components/include/z13/components` — flecs component structs shared across modules (`building.h`, `gameplay.h` (game flags; `gameplay_phases.h`, `player.h`, `player_parts.h` beside it), `bootstrap.h`, `status.h`, `z13.h`; station split by topic: `block.h`, `block_brush.h`, `station_mode.h`, `spawn_point.h`; input split by topic: `action_id.h`, `action_map.h`, `action_listener.h`, `input_config.h`, `input_events.h`, `input_phases.h`, `input_state.h`) and `constants/constants.h`.
- `src/lib_z13/modules/z13_module` — main gameplay module: `bootstrap/`, `gameplay/`, `input/` (`gameplay_input_system`, input config loading). Factory in `z13_module_factory.cpp`. Its own tests live under `tests/`, with the `Z13TestWorld` harness in `tests/support/z13_test_world.h`.
- `src/lib_z13/modules/bullet_module` — Bullet physics plugin. `bullet_components.h` exposes only `RigidBody` (component used outside the module); `PhysicsWorld` (the Bullet dynamics-world singleton) is declared in `physics_world.h`/`physics_world.cpp`, private to the module. Factory in `bullet_module_factory.cpp`.
- `src/lib_z13/modules/building_module` — building plugin, loaded right after `z13_module`: the ship scene's building tool, the block palette (`BlockPalette`), and in station mode (`StationMode`, `z13/components/station_mode.h`) filling a new scene from a blueprint (`assets/station/blueprints/`), the brush (`BlockBrush`) and building/destroying grid blocks (`Block` in `block.h`) through `BlockIndex` and `ValidateBuild`. Systems in detail: plan-and-debt skill, references/modules.md. Factory in `building_module_factory.cpp`.
- `src/lib_z13/modules/station_module` — station core plugin (`docs/station-systems-plan.md`): `RoomSystem` keeps the `RoomCache` singleton (rooms and portals, derived from the blocks by `rooms`) and `TopologyVersion` (state), `RoomOverlaySystem` fills `RoomOverlay` (F4); doors come with `f/doors`. Factory in `station_module_factory.cpp`.
- `src/lib_z13/primitives` — `primitives`, a stateless static library: parses/validates the block palette (`assets/station/palette.json`, schema `primitives.fbs`) into plain types (`BlockPalette` singleton, loaded by `building_module`) and blueprints (`blueprint.fbs`, blocks naming palette primitives) and the lights of lamp blocks (`lights.h`), and builds each primitive shape as convex solids in cell units, from which both the mesh and the collision hull come, and places it on the grid (`placement.h`: 24 orientations, occupied cells, pose).
- `src/lib_z13/rooms` — `rooms`, a stateless static library without flecs: `BuildRooms` splits the free space into rooms (column intervals, flood fill) and finds the portals of doors and windows; `RoomCache` (graphs by block fingerprint), `OverlapsBetween` (what a rebuild did to each room), `ReachOf` (a lamp's room and what it lights).
- `src/lib_z13/grid` — `z13_grid`, a stateless static library without flecs: `BlockIndex` (which block occupies which cells, filed by chunk; ray along the grid), kept by `building_module` as a singleton.
- `src/lib_raylib_module` — rendering/platform/GUI plugin: `render/` (environment, lamp lights (Forward+ clusters) and shadow maps, skybox, render resources; remote entities are drawn through `pose_smoothing`), `platform/` (SDL), `gui/` (ImGui windows/keybindings), `tools/` (asset loading, Assimp, input publishing, math conversion). Factory in `raylib_module_factory.cpp`.
- `src/lib_z13/z13_launcher` — loads module factories (`.so` plugins) into one flecs world at startup.
- `src/lib_z13/schemas/fbs` — FlatBuffers schemas for world (de)serialization (`lib_core/state/world_serializer.h`).
- `src/tests` — the `z13_test_runner` gtest binary; links module factories directly (see `Z13TestWorld`) rather than loading them as plugins.
- `src/zodiac13` — the actual game executable (`main.cpp`).

Each gameplay/render module (`building_module`, `bullet_module`, `station_module`, `raylib_module`, `z13_module`) is a hot-loadable plugin: a `ModuleFactoryBase` subclass exported via `BOOST_DLL_ALIAS`, whose `RegisterModules` does `world.import<XModule>()`. A module registers callbacks per world-creation stage (`OnRegisterComponents` → `OnInitPhases` → `OnInitSystems` → `OnInitWorldData`, `lib_core/world/lifecycle.h`), which `Core::CreateWorld` runs in order with defer suspended; see `bullet_module/src/physics_system.cpp` for a compact example, including why singleton values are set in the systems stage rather than next to their component registration. Don't hook these stages with observers: a system built while deferred keeps flecs' default `OnUpdate` phase next to its own. A module declares only the phase order it actually needs; `Core::CreateWorld` makes the order total (`lib_core/world/phase_order.h`), breaking ties between independent phases by phase path, so phase types must not live in an anonymous namespace.

## Flecs components

- Entity state is serialized through flecs meta reflection (reflect-cpp based, see `component_meta.h`), which only walks plain data — so a component must never hold a raw pointer, `unique_ptr`, or `shared_ptr` field.
  - Exception: a singleton component that wraps non-serializable engine/runtime machinery and is always rebuilt rather than saved/loaded (`PhysicsWorld`, `Skybox`, `RenderModel`, `Lighting`) is exempt and may keep a `shared_ptr`-backed pImpl.
- For per-entity state that wraps a native/engine object (a Bullet body, a GPU model, ...): make the component an empty tag, and own the actual object in a side table (`std::unordered_map<flecs::entity_t, T>`) kept by the owning singleton/system. `unordered_map` is node-based, so an element's address stays stable across insert/erase of other entries — no smart pointer needed even though the object itself can't move. See `bullet_module`'s `RigidBody` tag + `PhysicsWorld`'s internal `bodies` table for the pattern. Objects shared by every entity of one shape (collision shapes, meshes) are cached by shape instead, not per entity: `PhysicsWorld`'s shape cache, `raylib_module`'s `BlockMeshes`.
- Where a stable-address pImpl is still needed (the singleton exemption above), name the backing struct `State` (not `Impl`) and its handle member `state_`; the handle itself must be `shared_ptr`, not `unique_ptr` — flecs can in principle duplicate a component, and `unique_ptr` wouldn't tolerate that.
- State that must survive save/load or a network restore is marked inside the component struct with independent nested properties: `using State = void;` (empty structs become state tags) and, for singletons, `using Singleton = void;` (a nested type doesn't affect reflect-cpp; a C++ base class would break it). Register through `RegisterComponent<T>`/`RegisterComponents<...>` (`lib_core/state/world_state.h`), which applies the properties (reflect-cpp meta is built only for `State` components, so `Singleton`-only ones may hold `std::optional`, paths, etc.); new properties are added the same way. A singleton that belongs to one network session (queues, logs, clock sync, ...) also declares `using SessionScoped = void;`: `ResetSessionScopedComponents` resets all of them when a session starts or ends, so no reset list has to be maintained by hand. State components live on entities tagged `StateEntity`; a singleton with both properties on its component entity. Unmarked components, and singletons without `State`, are never state. Everything derived from state (native objects, side tables, helper entities) is rebuilt by systems that compare against those components every frame (a large set may gate the comparison on flecs change detection, see `SyncBlockBodies` in `bullet_module/src/physics_system.cpp`), not by `OnAdd`/`OnRemove`/`OnSet` observers; observers are for external events (input, save/load requests, lifecycle). Register through the helpers, not raw `.member()` chains: inside an observer flecs defers commands and each `.member()` overwrites the previous one (the helpers use `ImmediateScope`). A system that creates/destroys entities other systems read later in the frame declares `.write<...>()`, and one that checks components inside its body (`has<>`) declares `.read<...>()`; otherwise flecs defers the merge to the next frame.
- Station blocks enter and leave the world only through `SpawnBlock`/`DestroyBlock` (`z13/components/block_changes.h`): systems deriving from blocks (index, bodies, rooms) apply the `BlockChange` events these leave instead of walking every block, and resync in full only after a restore (`WorldRestores`). A system reading the events uses `BlockChangeReader` and declares `ClearBlockChangesPhase` after its own phase; a block changed in place is missed (`BlockChangeTest` catches it).
- An observer that reads a component's *value* listens on `flecs::OnSet`, not `OnAdd`: `.set<T>(v)` adds a default-constructed `T` (firing `OnAdd`) before assigning `v`.
- A system's `.immediate()` flag doesn't suspend command deferral; to create something and read it back in the same callback (e.g. spawn, then `CaptureState`), wrap it in `ImmediateScope`.
- `world.progress()` can't nest, so nothing called from a system may tick the world itself: re-simulating ticks goes through `RequestRollback`/`DeferRollback` (`lib_core/state/rollback.h`), which `TickWorld` applies between frames. A restore applies a snapshot captured earlier — don't let other deltas land on the same world while one is pending, or they're pruned as stale.
- Before reasoning about when a system runs, check its real schedule: `ecs_log_set_level(1)` around one `progress()` prints it; ids (and so tie order) differ between Debug and Release.
- Access a singleton component (`.add(flecs::Singleton)`) as a query/observer term directly — add it to the system's/observer's type list and take it as a parameter — rather than calling `world.ensure<T>()`/`get_mut<T>()` inside the callback body.

## Code style

- C++ code must follow the Google C++ Style Guide (https://google.github.io/styleguide/cppguide.html).
- Prefer idiomatic modern C++ over C-style code (e.g. `std::array`/`std::vector` over raw arrays, `std::string_view` over `char*`, RAII over manual resource management).
- String constants: declare as `constexpr std::string_view`, not `const char*`. When a C API (raylib/SDL/ImGui/flecs/...) needs a null-terminated `const char*`, call `.data()` on a `string_view` that's known to span a whole string literal (safe: the literal's own `\0` is the next byte) — never on a `string_view` that could be a substring or come from arbitrary input. For a `string_view` *parameter* (any caller, not just literals), materialize an owning `std::string` first and pass `.c_str()`. Fixed third-party callback signatures (e.g. `raylib::TraceLogCallback`) keep `const char*` as-is — they can't be changed.
- Avoid raw pointers where possible; prefer references, smart pointers (`std::unique_ptr`/`std::shared_ptr`), or non-owning views instead.
- Avoid static variables.
- Avoid exceptions for error handling; prefer `std::expected` instead. A result without a value is `z13::Status` (`lib_core/utils/status.h`), not a spelled-out `std::expected<void, std::string>`; a type repeated across the codebase likewise gets one shared alias.
- Log as little as possible inside helper functions: return failures through `std::expected` and let the business-logic caller (system, observer, handler) log once. This keeps logging in few places, so adding another sink (file, network, console) touches little code.
- Don't signal "no value" with a sentinel (`-1`, `0`, an empty string, ...); use `std::optional` instead, so absence can't be confused with a real value.
- One non-trivial class per `.h`/`.cpp` pair, declared in the header (GUI windows included), never only in a `.cpp` or its anonymous namespace. A pImpl `State` gets its own `x_state.h`/`.cpp` too, and no struct is declared inside a class or function: make it a namespace-level type in a header.
- Reference members are `std::reference_wrapper`, not `T&`.
- Library code (`lib_core`, module libraries) returns errors through `std::expected`; only systems, observers and the launcher log them.
- Default member initializers (every member, pointers included: `ecs_world_t* world_ {};`): use brace-init (`int x {};`, `bool y {};`) instead of `= 0`/`= false`; for Eigen members use `Type::Zero()` instead of `{0.f, 0.f}`.
- Don't expose `void*` or raw-pointer-plus-count pairs in APIs. Forward-declare the concrete type instead of erasing it to `void*`, and return a standard container/view (e.g. `const std::vector<T>&`, `std::span<T>`) instead of a pointer-and-length out-parameter.
- Group related scalar fields that travel together (e.g. width/height, x/y) into a single `Eigen::Vector2i`/`Eigen::Vector2f` rather than separate members.
- Vectors, points, colors and texels (anything with x/y/z/w components) are Eigen types (`Eigen::Vector3f`, `Eigen::Vector4f`, `Eigen::Array2f`, ...), never `std::array<float, N>`; `std::array` holds fixed collections of other things (texture ids, cube faces).
- Fields repeated across components or types go into one shared struct both hold (`BlockSpec` in `Block` and `BlockBrush`). A map key is a struct with named fields and a hash functor, never a `std::tuple`; a key or helper two modules need is defined once in a shared library.
- A constant (asset path, file name, CLI flag, scene name, ...) is defined once, by the code that owns it, and reused everywhere else, tests included; grep for an existing definition before declaring one. Tests reach shipped assets through `z13_tests/shipped_station.h`, not their own path constants.
- A small closed set of values (orientations, modes) is an `enum class` naming every value, not an integer with a count and `IsValid...()` checks: an invalid value then doesn't compile, and the snapshot codec rejects unknown constants.
- Don't reset, copy or compare an object member by member (`a_.clear(); b_.clear();`); reassign or compare the whole object, so a new member can't be forgotten.
- A `std::array` initialized from its elements lets the compiler deduce type and size (`std::array view_pos = {x, y, z};`, `constexpr std::array kModes {...};`), rather than spelling out `std::array<float, 3>`.
- Prefer std algorithms and ranges (`std::accumulate`, `std::ranges::transform`, ...) over hand-written loops, and Eigen over home-made matrix/vector types.
- Namespaces are named by domain (`z13::building::primitives`, not `z13::primitives`).
- Mark intentional `switch`/`case` fallthrough with `[[fallthrough]];`, also between stacked empty `case` labels.
- Prefer `std::format` over `snprintf`/manual char buffers or string concatenation (`+`) for building strings.
- Represent filesystem paths as named `std::filesystem::path` constants, not bare string literals passed inline.
- Avoid magic numbers, small ones included (a 2 for double buffering); give them a named `constexpr` constant whose name, or a short comment, says why it has that value. Numbers that tune behaviour (reach distances, clearances, chunk sizes) aren't constants: they go into a settings.json table (FlatBuffers schema with `min`/`max`/`cli`, cross-field rules in `ValidateSettings`), are sent to clients in `Welcome` like `PhysicsTuning`, and are read by systems as a singleton term.
- Always use braces for `if`/loop bodies, even single-statement ones.

## Working in this repo

- Some files are CRLF on purpose (e.g. `vcpkg.json`): edit them with the Edit tool, or as bytes / with `newline=''` in a script; a text-mode rewrite turns them into a whole-file diff.
- The player's experience comes first: every action the player performs is registered and reaches the simulation, never dropped, held back or collapsed into its last value (a resync, rollback or lag included; input the pause menu swallows is not a gameplay action). When a network or performance mechanism would cost input, change the mechanism, not the input (see `AdoptCatchUp`, which merges own input a Resync lacks).
- Render-side mechanisms (smoothing, effects) work for every rendered entity with the relevant component, keyed per entity in a side table — not special-cased for today's brush or camera; more meshes and animations are coming.
- Pre-alpha: changing the save/snapshot format needs no migration of player data; only regenerate saves checked into the repo or tests.
- Tests reuse the project's registration helpers (`RegisterComponent`, `RegisterComponentMeta`) and say why when they can't.
- The user opens and merges pull requests; push the branch and give the link.

## Comments

- Prefer writing clear, self-documenting code (good names, small functions) over explaining unclear code with comments.
- Avoid long comments/comment blocks, unless the logic being described is genuinely complex — one or two sentences is usually enough. This applies even to comments explaining a non-obvious design decision (e.g. why a type uses a pImpl, why a struct's field order matters) — state the reasoning tersely, don't write a paragraph.
- In implementation code, add a comment mainly when a non-obvious or debatable decision was made, to explain the reasoning for future readers.
- Don't restate reasoning that's already documented elsewhere in this file or the codebase (e.g. a project-wide convention) — reference it briefly instead of re-deriving it at every call site.
- The same applies to commit messages: keep them short, a couple of sentences is enough.
- Never include Claude Code session/conversation IDs or other internal tooling metadata in commit messages.

## Branch size

- A branch's total diff should average no more than 1000 lines of code. Exceeding this is acceptable only in exceptional cases.
- If a branch exceeds this limit, flag it immediately during code review. Branches of the station plans (`docs/station-primitives-plan.md`, `docs/station-systems-plan.md`) may exceed it when a stage can't be split meaningfully (user decision 2026-10-05): mention the size, don't treat it as a defect.

## Code review

- Every review also checks the comments the branch adds (`git diff main -U0 | grep '^+\s*//'`), moved ones included: each one that restates a name or the code, repeats another comment or a convention from this file, or runs longer than the "Comments" section allows is deleted or shortened in the same review, without being asked.

## Skills

Procedures live in `.claude/skills/`, loaded when the task needs them:

- `build-and-test` — building and testing, detached builds and their status in PRoot, stopping a stale build, CI checks before a push.
- `finish-branch` — code review (with comment trimming), commit, push, conflicts after a squash merge, updating main after a merge.
- `perf-measure` — benchmarks, trustworthy measurements on the dev box, prototyping in a benchmark, header width.
- `plan-and-debt` — where plans and debts are, what's next, recording a finished stage or resolved debt.
- `github-pr` — reading PR state and review comments through the GitHub API.
