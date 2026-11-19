# zodiac13

## Build & test

How to build, test and wait on builds is in the build-and-test skill; the rules:

- Requires `VCPKG_ROOT` set (vcpkg manifest mode; deps come from `vcpkg.json`). Configure + build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`; tests: `ctest --test-dir build` (`-LE heavy` leaves out the tests over 10 s in Debug, labelled `heavy` in `src/tests/CMakeLists.txt`: use it while iterating, the whole set before a push; a new test that runs over 10 s gets the label).
- Inside PRoot (`uname -a` shows `PRoot-Distro`): cap parallel jobs at `-j3` — higher counts have hung the session. A full vcpkg rebuild only with `VCPKG_MAX_CONCURRENCY=2` (`-j2`), launched detached. Long builds run detached with a log, never as a long foreground call.
- Never `pkill -f` a pattern that also appears in your own command line — it kills the calling shell.
- Debug builds compile the math libraries and the plugins that tick them (`rooms`, `primitives`, `grid`, `cell_light`, `touch`, `outline`, `bullet_module`, `atmos_module`, `net_module`: `OPTIMIZE_IN_DEBUG` in `z13_add_library` and `z13_add_plugin`) with `-O1` on GCC and Clang and `/O2` without runtime checks on MSVC, since Eigen (and Boost.Graph, which `rooms` no longer uses) run 8-70 times as slowly at `-O0`; `-DZ13_DEBUG_OPTIMIZE=OFF` turns it off for debugging one of them. A new library of heavy math gets the same flag.
- Sources and schemas are listed explicitly in each `CMakeLists.txt` (no globs): add a new `.cpp`/`.fbs` there; CI's `.github/scripts/check_listed_sources.py` fails on an unlisted one.
- flecs, spdlog, fmt, Tracy's client (overlay `ports/tracy`) and `lib_core` (`z13_core`) are shared libraries (`triplets/`) so every plugin sees one process state; a dependency with process-wide state that plugins share must be shared too (CI's `check_duplicated_state.py`, ctest `PluginSmoke`). Editing a triplet rebuilds every dependency; per-port flags go in an overlay port (`ports/`).

## Codebase map

- `src/lib_profile` — `z13_profile`: `Z13_PROFILE_ZONE`/`Z13_PROFILE_FRAME`, Tracy zones only with `Z13_PROFILE` (perf-measure skill).
- `src/lib_core` — `z13_core` shared library, flecs/module plumbing: `commands/`, `world/` (lifecycle stages, phase order, module factory base), `time/`, `state/` (markers, snapshots, rollback, save/load), `settings/`, `utils/` (log, `Assets`, `ThreadPool`, frame report).
- `src/lib_z13/components/include/z13/components` — flecs component structs shared across modules (`constants/constants.h` beside).
- `src/lib_z13/modules/` — hot-loadable plugins, each with a `*_module_factory.cpp`: `z13_module` (main gameplay, input, console), `building_module`, `bullet_module` (physics), `station_module` (rooms, interaction, switches), `door_module`, `lift_module`, `conduit_module`, `power_module`, `atmos_module`, `gravity_module`, `door_render_module`, `conduit_render_module`, `mcp_module` (debug channel), `net_module`; load order and what each does: `docs/codebase-map.md`, `docs/station-systems-plan.md`.
- `src/lib_z13/<library>` — stateless static libraries, mostly without flecs: `primitives` (block palette, shapes, placement), `rooms`, `networks`, `power`, `atmos`, `cell_light`, `grid`, `sectors`, `outline`, `input`, `player`, `console_input`, `touch`; `schemas/fbs` holds FlatBuffers schemas more than one library uses.
- `src/lib_raylib_module` — rendering/platform/GUI plugin (`render/`, `platform/`, `gui/`, `tools/`).
- `src/lib_z13/z13_launcher` — loads module factories (`.so` plugins) into one flecs world at startup.
- `src/tests` — the `z13_test_runner` gtest binary; links module factories directly (see `Z13TestWorld`).
- `src/zodiac13` — the game executable (`main.cpp`).

Per-module detail (what each system does, which singleton lives where): [docs/codebase-map.md](docs/codebase-map.md).

Each gameplay/render module (`building_module`, `bullet_module`, `station_module`, `door_module`, `lift_module`, `conduit_module`, `power_module`, `atmos_module`, `gravity_module`, `conduit_render_module`, `door_render_module`, `raylib_module`, `z13_module`) is a hot-loadable plugin: a `ModuleFactoryBase` subclass exported via `BOOST_DLL_ALIAS`, whose `RegisterModules` does `world.import<XModule>()`. A module registers callbacks per world-creation stage (`OnRegisterComponents` → `OnInitPhases` → `OnInitSystems` → `OnInitWorldData`, `lib_core/world/lifecycle.h`), which `Core::CreateWorld` runs in order with defer suspended; see `bullet_module/src/physics_system.cpp` for a compact example, including why singleton values are set in the systems stage rather than next to their component registration. Don't hook these stages with observers: a system built while deferred keeps flecs' default `OnUpdate` phase next to its own. A plugin never includes another plugin's headers (CI's `check_plugin_includes.py`): what two plugins share goes to a component header, or to a stateless library (`input`, `player`). A module declares only the phase order it actually needs; `Core::CreateWorld` makes the order total (`lib_core/world/phase_order.h`), breaking ties between independent phases by phase path, so phase types must not live in an anonymous namespace.

## Flecs components

- Entity state is serialized through flecs meta reflection (reflect-cpp based, see `component_meta.h`), which only walks plain data — so a component must never hold a raw pointer, `unique_ptr`, or `shared_ptr` field.
  - Exception: a singleton component that wraps non-serializable engine/runtime machinery and is always rebuilt rather than saved/loaded (`PhysicsWorld`, `Skybox`, `RenderModel`, `Lighting`) is exempt and may keep a `shared_ptr`-backed pImpl.
- For per-entity state that wraps a native/engine object (a Bullet body, a GPU model, ...): make the component an empty tag, and own the actual object in a side table (`std::unordered_map<flecs::entity_t, T>`) kept by the owning singleton/system. `unordered_map` is node-based, so an element's address stays stable across insert/erase of other entries — no smart pointer needed even though the object itself can't move. See `bullet_module`'s `RigidBody` tag + `PhysicsWorld`'s internal `bodies` table for the pattern. Objects shared by every entity of one shape (collision shapes, meshes) are cached by shape instead, not per entity: `PhysicsWorld`'s shape cache, `raylib_module`'s `BlockMeshes`.
- Where a stable-address pImpl is still needed (the singleton exemption above), name the backing struct `State` (not `Impl`) and its handle member `state_`; the handle itself must be `shared_ptr`, not `unique_ptr` — flecs can in principle duplicate a component, and `unique_ptr` wouldn't tolerate that.
- State that must survive save/load or a network restore is marked inside the component struct with independent nested properties: `using State = void;` (empty structs become state tags) and, for singletons, `using Singleton = void;` (a nested type doesn't affect reflect-cpp; a C++ base class would break it). Register through `RegisterComponent<T>`/`RegisterComponents<...>` (`lib_core/state/world_state.h`), which applies the properties (reflect-cpp meta is built only for `State` components, so `Singleton`-only ones may hold `std::optional`, paths, etc.); new properties are added the same way. A singleton that belongs to one network session (queues, logs, clock sync, ...) also declares `using SessionScoped = void;`: `ResetSessionScopedComponents` resets all of them when a session starts or ends, so no reset list has to be maintained by hand. A state component with a `player_id` whose entities go wherever that player's entity goes (carried items, ...) declares `using PlayerBound = void;`: a player entering a client's interest brings them (`EachPlayerBound`). State components live on entities tagged `StateEntity`; a singleton with both properties on its component entity. Unmarked components, and singletons without `State`, are never state. Everything derived from state (native objects, side tables, helper entities) is rebuilt by systems that compare against those components every frame (a large set may gate the comparison on flecs change detection, see `SyncBlockBodies` in `bullet_module/src/physics_system.cpp`), not by `OnAdd`/`OnRemove`/`OnSet` observers; observers are for external events (input, save/load requests, lifecycle). Register through the helpers, not raw `.member()` chains: inside an observer flecs defers commands and each `.member()` overwrites the previous one (the helpers use `ImmediateScope`). A system that creates/destroys entities other systems read later in the frame declares `.write<...>()`, and one that checks components inside its body (`has<>`) declares `.read<...>()`; otherwise flecs defers the merge to the next frame.
- A snapshot fingerprints every state entity by one scheme (`FingerprintOf`, `world_serializer.cpp`; docs/snapshot-capture.md): its name and the raw bytes of its components, which is why a state component stays flat data (numbers, enums, Eigen, `std::array`, structs of them); a string, an entity reference or another opaque member makes its entities re-encode on every snapshot. A new entity kind gets no hash, cache or watch of its own: no per-kind `HashOf`, and an exception to the scheme needs the user's decision. What a group publishes (`GroupSnapshot::fingerprint`, compared between worlds) is hashed from the encoded values, never raw bytes (padding differs). A write to a state component through `get_mut`/`ensure` is followed by `modified<T>()`, or the change watch misses it.
- Station blocks enter and leave the world only through `SpawnBlock`/`DestroyBlock` (`z13/components/station/block_changes.h`): systems deriving from blocks (index, bodies, rooms) apply the `BlockChange` events these leave instead of walking every block, and resync in full only after a restore (`WorldRestores`). A system reading the events uses `BlockChangeReader` and declares `ClearBlockChangesPhase` after its own phase; a block changed in place is missed (`BlockChangeTest` catches it).
- An enum a component holds is declared in a named namespace, tests included: under clang (the Android NDK's, the `x64-linux-clang` CI job) flecs' enum reflection takes `(anonymous namespace)::E::kX` for an invalid cast and registers no constants.
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
- The exception to Eigen for vectors is code that runs every frame for hundreds of objects or more (culling, picking, per-portal or per-prop math): a Debug build, which we develop and measure in, keeps every Eigen temporary and such a loop costs 3-5 times as much, so it uses plain structs and arrays of `float` (`Frustum::Intersects`, `Through` in `room_visibility.cpp`). Each such place says in a short comment why it is not Eigen, so nobody "tidies" it back; measure first (`FrameWalkBench`, `RenderPrepBench`) before taking Eigen out of anything else.
- Fields repeated across components or types go into one shared struct both hold (`BlockSpec` in `Block` and `BlockBrush`). A map key is a struct with named fields and a hash functor, never a `std::tuple`; a key or helper two modules need is defined once in a shared library.
- A constant (asset path, file name, CLI flag, scene name, ...) is defined once, by the code that owns it, and reused everywhere else, tests included; grep for an existing definition before declaring one. Tests reach shipped assets through `z13_tests/shipped_station.h`, not their own path constants.
- A small closed set of values (orientations, modes) is an `enum class` naming every value, not an integer with a count and `IsValid...()` checks: an invalid value then doesn't compile, and the snapshot codec rejects unknown constants.
- Don't reset, copy or compare an object member by member (`a_.clear(); b_.clear();`); reassign or compare the whole object, so a new member can't be forgotten.
- A `std::array` initialized from its elements lets the compiler deduce type and size (`std::array view_pos = {x, y, z};`, `constexpr std::array kModes {...};`), rather than spelling out `std::array<float, 3>`.
- Prefer std algorithms and ranges (`std::accumulate`, `std::ranges::transform`, ...) over hand-written loops, and Eigen over home-made matrix/vector types.
- Don't index the same element more than once in a row (`nodes_[i].a = ...; nodes_[i].b = ...;`, `if (flags_[i] == 0) {...} flags_[i] = 0;`): take a reference once (`auto& node = nodes_[i];`) or build the value and insert it whole. A reference doesn't outlive a call that can grow the container: index again after one.
- Namespaces are named by domain (`z13::building::primitives`, not `z13::primitives`).
- Mark intentional `switch`/`case` fallthrough with `[[fallthrough]];`, also between stacked empty `case` labels.
- Prefer `std::format` over `snprintf`/manual char buffers or string concatenation (`+`) for building strings.
- Represent filesystem paths as named `std::filesystem::path` constants, not bare string literals passed inline.
- Tolerances and epsilons (`1e-5f`, `1e-3f`) are named constants whose name or comment says why they have that size, or an existing one (`z13::math::kEpsilon`), in tests too: a literal in `EXPECT_NEAR` or `isApprox` is a magic number.
- Avoid magic numbers, small ones included (a 2 for double buffering); give them a named `constexpr` constant whose name, or a short comment, says why it has that value. Numbers that tune behaviour (reach distances, clearances, chunk sizes) aren't constants: they go into a settings.json table (FlatBuffers schema with `min`/`max`/`cli`, cross-field rules in `ValidateSettings`), are sent to clients in `Welcome` like `PhysicsTuning`, and are read by systems as a singleton term.
- Always use braces for `if`/loop bodies, even single-statement ones.

## Working in this repo

- Some files are CRLF on purpose (e.g. `vcpkg.json`): edit them with the Edit tool, or as bytes / with `newline=''` in a script; a text-mode rewrite turns them into a whole-file diff.
- Gameplay mechanics follow Space Station 13, reworked and simplified (`docs/ss13-reference.md`): a gameplay decision says how SS13 does it and why ours differs, and each new difference gets a line there. Only mechanics are taken — never code, art, sounds or texts from SS13 codebases (AGPL / CC BY-SA).
- A fixed port of a local service (debug channel, tools) goes below 32768, outside the ephemeral range (32768-60999 on Linux and Android) the system hands to outgoing connections, so a connection of ours can't take it first; the port is a setting with a `cli`, and 0 means "any free one" for tests.
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
- If a branch exceeds this limit, flag it immediately during code review. Branches of the station plans (`docs/station-primitives-plan.md`, `docs/station-systems-plan.md`) may exceed it when a stage can't be split meaningfully (user decision 2026-10-05): mention the size, don't treat it as a defect. The hub's grid chain (`docs/multi-grid-plan.md`, "Наклонный модуль": filter, body, mesh, blueprint, docking) is one branch by the same kind of decision (user, 2026-11-01).

## Code review

- Every review also checks the comments the branch adds (`git diff main -U0 | grep '^+\s*//'`), moved ones included: each one that restates a name or the code, repeats another comment or a convention from this file, or runs longer than the "Comments" section allows is deleted or shortened in the same review, without being asked.

## Skills

Procedures live in `.claude/skills/`, loaded when the task needs them:

- `build-and-test` — building and testing, detached builds and their status in PRoot, stopping a stale build, CI checks before a push.
- `finish-branch` — code review (with comment trimming), commit, push, conflicts after a squash merge, updating main after a merge.
- `perf-measure` — benchmarks, trustworthy measurements on the dev box, prototyping in a benchmark, header width.
- `plan-and-debt` — where plans and debts are, what's next, recording a finished stage or resolved debt.
- `station-geometry` — drawing walls, floors and ceilings in station blueprints (generate_hub.py): joints, panel limits, the wall-joint checker, what to do about chamfers, burrs and teeth.
- `github-pr` — reading PR state and review comments through the GitHub API.
