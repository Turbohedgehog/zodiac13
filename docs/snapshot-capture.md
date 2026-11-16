# Снимок состояния: что и как кодируется, во что обходится

Описание текущего устройства (2026-11-15) и замер на хабе. Повод: хост-сервер рывками (каждый 60-й тик 100-150 мс), см. `hub-wiring-notes.md`.

## Когда берётся

`WorldSnapshotHistory::CaptureSnapshot` (`lib_core/state/world_snapshot_history.cpp`, `PostFrame`) раз в `snapshot_interval_seconds` (1 с = 60 тиков), `SnapshotCaptureRate::per_interval` раз за интервал. Одиночная игра ставит ставку 0 (launcher), сетевая сессия -- `net.rollback_snapshots_per_interval`. `SnapshotCaptureRate` -- `SessionScoped`, его значение по умолчанию 1, а не 0: не проверено, берёт ли одиночная игра снимки после выхода из сессии (`ResetSessionScopedComponents`). В сессии сразу после снимка `StateDigest::Record` считает дайджест того же тика.

## Из чего состоит снимок

`CaptureState` -> `CaptureImpl` (`world_serializer.cpp`), две независимые части:

1. **`CaptureUngrouped`**: все именованные сущности со `StateEntity` без компонента-члена группировки (`Block`). Запрос строится заново на каждом снимке. Для сущности:
   - `hash_ungrouped(e)` (у хаба только `HashOfWallEntity`: ровно три компонента -- имя, `StateEntity`, один компонент стены) и совпадение с кэшем -- сущность берётся из кэша;
   - иначе `CaptureEntity`: обход всех компонентов, `EncodeValue` (reflect-cpp в байты) каждого state-компонента, связи, путь строкой -- полное кодирование;
   - если `WallWatch` (`hashed_unchanged`) говорит, что компоненты стен не менялись, их сущности не посещаются, а берутся целым отсортированным списком (`reuse_members`).
2. **`CaptureGroups`**: блоки. Группа -- `(grid, chunk)`, чанк `index_chunk_cells` (`MakeGroupOf` в `building_module/block_snapshot_system.cpp`).
   - `BlockWatch::Unchanged` (change detection запросов по `Block` и `SpawnPoint` плюс счёт блоков, чтобы поймать удаление): не менялись -- группы прошлого снимка берутся как есть;
   - иначе `ScanGroups` проходит по всем блокам, `HashOf` (имя, `Block`, `SpawnPoint`) складывается в отпечаток группы; группа с прежним отпечатком переиспользуется, остальные кодируются.

Снимок -- `WorldSnapshot{entities, groups}` из `shared_ptr`, поэтому неизменённое делится между снимками.

## Хэши (три разных, не связаны)

- `HashOf` блока и отпечаток группы (`building_module`), вручную, покрывает только известные компоненты.
- `HashOfWallEntity`, вручную, для кэша ungrouped.
- `HashSnapshot` (`lib_core/state/snapshot_hash.cpp`): FNV по закодированным байтам всех ungrouped-сущностей плюс отпечатки групп; цена растёт с их числом и размером.
- В `ComputeStateDigest` (`net_module/protocol/state_digest.cpp`) сверх этого: пути всех ungrouped-сущностей (`e.path()`), сортировка, `HashBySector` (сектор -- куб `net.sector_size_cells`, кратный чанку) и хэши по гридам, позиции игроков.

## Что не обобщено

Единого правила для новой сущности нет. Всё, что не `Block` и не стена, попадает в ungrouped без кэша и кодируется заново на каждом снимке; кэш получает только тот, кто вручную пишет `HashOf`, группировку и `Watch`, как блоки и стены.

## Замер: хаб, Debug, Windows (`SnapshotCostBench.DISABLED_HubSnapshot`)

Мир загружен, 300 тиков, затем снимки через каждые 60 тиков (`z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='SnapshotCostBench.*'`).

| Шаг | Время |
|---|---|
| первый `CaptureState` (кэши пусты) | 560-580 мс |
| `CaptureState` после 60 тиков | 75-110 мс |
| из него кодирование ungrouped (на ~1100 сущностях) | 62-72 мс |
| из него блоки (783 группы, не менялись) | 0-10 мс |
| `HashSnapshot` | 3-6 мс |
| `ComputeStateDigest` | 21-30 мс |

Снимок хаба: 3103 ungrouped-сущности и 783 группы. Ungrouped: 1036 `WallNode`, 908 `WallSegment`, 48 `WallOpening` (кэш по хэшу, `reuse_members` после второго снимка), 1013 `Conduit` и ~100 прочих (`RoomAirEntry` 74, `Dock`, игроки, предметы, лифт).

**Кодируются заново на каждом снимке 1111 сущностей: 1013 `Conduit` без хэша (они не меняются после постройки) и ~100 прочих.** Это около 0,06 мс на сущность, 90% времени `CaptureState`. Блоки и стены при неизменной станции почти ничего не стоят.

Чем заняты 21-30 мс `ComputeStateDigest` (пути 3103 сущностей и сортировка, `HashBySector`), отдельно не замерено.

Разбивка по типам "каждая сущность кодируется отдельно" в бенче (`CaptureEntityState`, ~0,9 мс на сущность) завышена: каждый вызов заново проходит весь мир. Верны только числа сущностей и байтов по типам.
