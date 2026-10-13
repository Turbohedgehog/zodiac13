# Station building module in detail

`src/lib_z13/modules/building_module` (docs/station-primitives-plan.md, "Модули строительства и станции"), loaded right after `z13_module`; factory in `building_module_factory.cpp`.

- The building tool and its input (`building_system`, `building_input_system`): the ship scene's free cubes.
- The block palette (`palette_system`): loads `assets/station/palette.json` into the `BlockPalette` singleton.
- Station mode: the `StationMode` singleton (`z13/components/station_mode.h`), set by `--station`/`--station-scene` or the menu, with `StationSceneChoice` naming the blueprint a new game starts from.
- `StationSceneSystem` fills a new station scene with that blueprint (`assets/station/blueprints/<scene>.json`: `site` a construction site, `test` the test station written by `docs/station-layout/generate_layout.py`, `spire` by `generate_spire.py`; none leaves it empty) through `PopulateSceneEvent`, which `GameplaySystem::OnInit` emits before the local player spawns.
- `BrushSystem` applies the brush actions (pick, turn) to the player's `BlockBrush` (`z13/components/block_brush.h`).
- `BlockBuildingSystem` builds (on release, sized by the drag) and destroys grid blocks (`Block` in `z13/components/block.h`) through `SpawnBlock`/`DestroyBlock`, the derived `BlockIndex` (`z13_grid`) and `ValidateBuild`, and leaves a `BrushPreview` on each brush for the renderer.
- `BlockSnapshotSystem` groups blocks into snapshot groups by index chunk (`lib_core/state/snapshot_grouping.h`).
