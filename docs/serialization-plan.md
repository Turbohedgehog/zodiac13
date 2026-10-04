# План: один формат сериализации снимков

## Контекст

Снимок мира (`WorldSnapshot`, `lib_core/world_serializer.h`) — сущности с тегами, компонентами и связями; значение компонента до этапа 2 было JSON-текстом из рефлексии flecs (`world.to_json()`), теперь — двоичное. До этого плана снимок и списки действий ехали в сети msgpack-блобами (reflect-cpp) внутри полей `[ubyte]` FlatBuffers-сообщения — два формата в одном протоколе. Откат (`WorldSnapshotHistory`) хранит снимки структурами в памяти, сохранения (`WorldJsonStore`) — JSON для чтения глазами.

Цель — один двоичный формат (FlatBuffers) для сети и двоичных сохранений, а затем — двоичные значения компонентов: снимок `Welcome`/`Resync` растёт вместе с базой, а сервер — микрокомпьютер на ~200 игроков (`network-scaling-plan.md`).

## Порядок работ (одна ветка на этап)

1. ✅ **`f/flatbuffers-snapshot`** (~500): схема `lib_core/fbs/world_snapshot.fbs`, `ToFlatbuffer`/`FromFlatbuffer`; `Welcome`/`Resync` несут снимок таблицей, `actions`/`held_values`/`pending` — `[ActionRecordWire]`, `PlayerJoined` — снимок одной сущности (`CaptureEntityState`); `SaveWorldState`/`LoadWorldState` — буфер FlatBuffers с `Verifier`; msgpack убран из кода и из `vcpkg.json`; `kProtocolVersion` 3. Значения компонентов пока JSON. Замер (Debug, 30 сущностей): цикл записи+чтения на четверть быстрее JSON, но размер тот же (1,01×) — почти весь объём в JSON-значениях.
2. ✅ **`f/binary-component-values`**: значение компонента — байты `EncodeValue` (`lib_core/component_codec.h`), записанные обходом мета-типа flecs: примитивы little-endian (pointer-sized — 8 байт), строки и коллекции с длиной `u32`, сущности путём; структуры, массивы, enum/bitmask и opaque поверх примитива/массива/вектора (`std::string`, `Eigen::Matrix4f`). Векторы flecs и opaque-структуры — явная ошибка (в state-компонентах их нет). Чтение недоверенных байтов проверяет границы, счётчики и хвост. JSON остаётся для `WorldJsonStore` (конвертирует значения через мир: `ToJson`/`FromJson` принимают его) и для дампа `CrossPlatformStateTest`; `kProtocolVersion` 4. Тесты — `component_codec_test.cpp` и `BinaryValuesShrinkTheSnapshot`. Двоичный путь точнее JSON: flecs пишет double вроде `-2.5e-300` как `-0`. Замер (Debug, ARM, 30 сущностей тестового мира): снимок FlatBuffers 7,5 КБ против 8,3 КБ с JSON-значениями (0,91×) — остаток в путях типов и имён; против сохранения `WorldJsonStore` — 0,33× по размеру и в 10 раз быстрее цикл записи+чтения.
3. ✅ **`f/snapshot-type-dictionary`**: пути типов, тегов и связей пишутся в буфер по одному разу — атрибут `(shared)` у строковых полей схемы (`CreateSharedString` в `Pack`), тег обёрнут в таблицу `Tag`, потому что `(shared)` бывает только у одиночной строки; `kProtocolVersion` 6. Замер (Debug, ARM): 30 сущностей — 7,5 КБ → 4,6 КБ (0,61×), 10 000 — 2,52 МБ → 1,43 МБ (0,57×); запись медленнее примерно на четверть из-за поиска общих строк, полный `SaveWorldState` не изменился (его время — захват и сортировка).

## Открытые вопросы

- Сжатие снимков (`network-scaling-plan.md`, этап `f/net-compression`) после этапа 2 даст меньше, чем сейчас на JSON: замерить заново.
