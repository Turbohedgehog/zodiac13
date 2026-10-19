# Plans in docs/

| Document | Covers | Where to look |
|---|---|---|
| `client-server-plan.md` | ENet client-server: sync model, protocol, lifecycle; its six stages are merged | "Технический долг" — the project-wide debt list |
| `net-core.md` | the net core's model (tick function and inputs), invariants И1–И8, audit findings Н1–Н11 | "Порядок работ" (`f/net-convergence-test`, `f/net-final-horizon` done → `f/net-clock-jump` …), "Ревизия избыточного кода" |
| `net-scenarios.md` | the net's scenarios step by step (start, join, play, clock, resync, hitches, loss, leave, late join): who sends what, settings, failures | reference, no stages; update with the code it describes |
| `code-revision-plan.md` | revising every module for redundant mechanisms against its core logic | "Порядок работ" (net first, then one branch per module), "Кандидаты" |
| `net-simplification-plan.md` | simplifying the network session code | "Порядок работ" (stages 1-4 done), "Отложено до замеров" |
| `action-delivery-plan.md` | delivering look/actions and smoothing remote players | stage 2 `f/look-hints` open; "Открытые вопросы" |
| `remote-sync-research.md` | measurements behind remote smoothing | research, no stages |
| `network-sync-plan.md`, `state-restore-plan.md`, `serialization-plan.md` | action log + snapshot history, save/restore, one snapshot format | done; background for the code |
| `network-scaling-plan.md` | scaling the network to ~200 players (sectors, interest) | "Порядок работ", "Открытые вопросы" |
| `server-plan.md` | dedicated server: session phases, lobby, finalizer, admin console, mods, Docker | "Порядок работ", "Риски и открытые вопросы" |
| `console-plan.md` | quake-like console | stage 1 done |
| `main-menu-plan.md` | main menu | — |
| `base-building-plan.md` | base building overall; stations parts superseded by the two station plans | stages `f/build-tools`, `f/build-undo`, `f/base-save`, `f/base-net` |
| `station-primitives-plan.md` | block primitives, palette, test stations, load measurements | stages 0-7 done; "Замеры нагрузки", snapshot step (в) open |
| `station-systems-plan.md` | rooms, render culling, lights, gas, power, doors, gravity, thread pool | "Порядок работ" (20 stages, many done), dependency line, "Открытые вопросы" |
| `performance-research-plan.md` | long operations, profiling tools, load agents | table "Долгие операции", "Решения под вопросом", stages `f/profiling-tools` → `f/perf-baseline` |
| `multi-grid-plan.md` | several grids (asteroids, rotated builds) | stages `f/grid-model` ... `f/grid-docking`; planned before `f/utility-contract` |
| `oblique-walls-plan.md` | oblique walls and space partition | stages 0-1 done (partition bench, incremental rooms) |
| `skew-station-plan.md` | the `shard` station: a small fragment of the future big station, with walls at different angles; grows with the oblique-walls stages | zones table, "Порядок работ" |
| `station-import-plan.md` | importing the Blender-built station into the game: the map export contract and the map-to-blueprint converter | "Порядок работ", "Открытые вопросы" |
| `dynamic-objects-plan.md` | items, kinematics, debris, asteroids | stage 0 `f/dynamic-bench` first |
| `android-plan.md` | Android build and touch controls: joysticks, on-screen inventory, the building tool as an item, context actions | "Порядок работ" (`f/android-build` first), "Открытые вопросы" |
| `asset-pack-plan.md` | packing assets/ and config/ into one archive read on every platform (release packages, APK without unpacking) | stages `f/asset-source` → `f/asset-pack`, "Открытые вопросы" |
| `ss13-reference.md` | Space Station 13 as the gameplay reference: what is repeated, and each difference with its reason | table "Отличия", grows with every gameplay decision |
| `station-layout/` | generators of the shipped station blueprints (`generate_layout.py`, `generate_spire.py`) | scripts, deck plans |
