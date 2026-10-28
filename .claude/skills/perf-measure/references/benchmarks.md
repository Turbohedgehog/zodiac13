# Benchmarks

All run over every shipped station blueprint (`site`, `spire`, `test`); `spire` is the large one (~6200 blocks, ~650 rooms).

| Benchmark | File | Measures |
|---|---|---|
| `RoomBuildBench.FullRebuild` | `station_module/tests/bench/room_build_bench_test.cpp` | full room build (`BuildRooms`): rooms, portals, leaves, time |
| `RoomEditBench.BuildAndDestroyAWall` | `station_module/tests/bench/room_edit_bench_test.cpp` | the tick that builds or destroys a wall (largest room, smallest room, outside the hull, a station wall), per system |
| `OpeningBench.FullSearch`, `.EditSearch` | `station_module/tests/bench/opening_bench_test.cpp` | `FindOpenings` over a whole station and near RoomEditBench's edits |
| `StationLoadBench.Scales`, `.Join`, `.ServerClient` | `z13_module/tests/bench/station_load_bench_test.cpp` | tick, snapshot capture, rollback at several station scales; a client joining; a server with a walking client |
| `EmptySectorBench.Queries` | `bullet_module/tests/bench/empty_sector_bench_test.cpp` | the player's queries and the building of the Bullet world with the bodies of all copies of a station against those of the first copy alone: what blocks nobody is near cost |
| `RenderChunkBench.ChunkSizes` | `z13_module/tests/bench/render_chunk_bench_test.cpp` | render chunk meshes at several chunk sizes, without GPU upload |
| `StationLoadBench.DISABLED_LateJoin` (opt-in: `--gtest_also_run_disabled_tests`, play time in `$Z13_LATE_JOIN_SECONDS`, default 60) | `z13_module/tests/bench/station_load_bench_test.cpp` | a client joining a server that played and built on `spire` for a while, in real time: Welcome size, load, dropped ticks, lag, when it caught up (docs/net-scenarios.md, scenario 10) |
| `SyncBench.*` (`RunAndStop`, `Strafe`, `LookSweep`, `Circle`, `Mixed`) | `z13_module/tests/bench/sync_bench_test.cpp` | remote player smoothness scenarios; CSVs to `$Z13_SYNC_BENCH_DIR` (docs/remote-sync-research.md) |

The renderer is measured in the game itself: `--render-tour FILE.csv` with `--station-scene` (and `--render-chunk-cells`, `--render-tour-view x,y,z,yaw,pitch`, `--fps 1000`), see docs/station-systems-plan.md, stage `f/render-measure`. It needs a display, so it runs on the user's machine, not in PRoot.

The table of long operations and their latest numbers is in docs/performance-research-plan.md, "Долгие операции".
