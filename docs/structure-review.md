# Ревью структуры (2026-11-02, `f/structure-review`)

Сделано одной веткой по решению пользователя (ветка выходит за 1000 строк: в основном перенос файлов и правка включений).

- Плагины не включают заголовки друг друга: `input` (`LoadInputConfig`, согласование действий) и `player` (`SpawnPlayer`, `camera_look`) — статические библиотеки без состояния; проверка `.github/scripts/check_plugin_includes.py`.
- `DoorInterlock` лежит в `z13/components/station/door.h`, `station_module` не знает про газ. Двери — плагин `door_module` (после `station_module`), `UtilityConsumersPhase` объявляет `InteractionSystem`. `GravitySystem` и `RoomGravity` переехали в `gravity_module`.
- `z13/components` разложены по `input`, `station`, `utility`, `gameplay`; `net_module/src` — по `transport`, `discovery`, `protocol`, `sectors`, `session`; `environment_render_system.cpp` разрезан на `scene_models`, `scene_overlay`, `scene_camera`, `remote_players`, `frame_lighting`, `chunk_sync`, `render_tour_step`.
- В настройки ушли: `atmos.overlay_*_percent`, `physics.spawn_spacing`/`spawn_clearance`, `building.max_slide_cells`, `net.ghost_life_intervals`/`history_horizons`, `render.sun_*`, `render.ship_*`, `render.avatar_radius_m`, `render.sun_shadow_*`. `kProtocolVersion` 34.

Вторая ветка (`f/tuning-constants`): в настройки ушли `physics.fly_speed` и `physics.max_pitch_deg` (скорость и предел тангажа свободной камеры, `CameraMoveAxes::speed`/`max_pitch_deg`), `net.max_clients` (`ServerTransportFactory` получила третий параметр), допуски и стрелка кисти стен (`building.wall_pick_slack_cells`, `wall_plane_slack_cells`, `wall_arrow_cells`, `wall_arrow_head_cells`). `kProtocolVersion` 36.

Осталось (именованные константы, не настройки): `kProbeCellsAbove` отладочного оверлея, ближняя плоскость куба теней (`DrawFaces` не получает настроек), цвета подсветки кисти и ламп, вёрстка GUI в em, форматы и пределы протокола. Рендер собран, но не запускался (нет видеокарты).
