# План: упрощение сетевого кода

## Контекст

Ревью сетевого кода (2026-10-04) показало: модель синхронизации стройная (состояние = функция от снимка и упорядоченного журнала команд; join, resync и опоздавшая команда идут через один откат), но вокруг неё накопились слои. `net_module` — ~3,4 тыс. строк кода, из них 1186 в `net_session_system.cpp` (сервер, клиент, жизненный цикл, настройки и расписание join/leave в одном файле). Тестов сети больше, чем кода (~4,2 тыс. строк).

Ядро не трогаем: `CommitDueCommands`, единый путь отката, `ReplayInProgress`, `StateDigest`, окно приёма команд.

## Порядок работ (одна ветка на этап)

1. **`f/net-session-split`** (без смены поведения, тесты те же):
   - `net_session_system.cpp` делится на `server_session`, `client_session`, `session_deltas` (расписание и история `PlayerJoined/Left`), `session_settings` (`AdoptedSettings`, валидация настроек сервера) и тонкий `net_session_system` с жизненным циклом.
   - `NetSession`: проверка `IsOpen()` в каждом методе заменяется на assert (закрытая сессия — ошибка последовательности вызовов, см. комментарий в `ServiceNetSession`); `NetSession::Result` везде становится `void`, пропадает каскад `if (!x) return x;`.
   - `HandleWelcome`: общий путь «ошибка → `Failed` → `EndSession`» вместо пяти копий.
   - `Welcome` и `Resync` — одна таблица с опциональными `player_id`/настройками (`kProtocolVersion`), один `FillCatchUp`/`DecodeCatchUp` без шаблонов.
   - Повторяющиеся `ToWire`/`FromWire` записей действий и `IsKnownActionId` — в одно место.

   Сделано. Отличия: `Welcome` и `Resync` не слиты в одну таблицу, а оба несут общую таблицу `CatchUp` (снимок, лог, удерживаемые значения, очередь; `kProtocolVersion` 8) — у `Welcome` остаются обязательные id игрока и настройки; кроме перечисленных файлов есть `catch_up` (сборка и приём `CatchUp`), `session_control` (`EndSession`, `FailSession`, статус соединения) и `wire_records`; проверки `IsOpen()` убраны совсем, без assert (решение на ревью): `NetSession` лежит в мире, только пока сессия открыта; `NetSession::Service` отдаёт события без `std::expected`, а ошибкой сессии сервера остаётся только неудачный захват снимка для `Welcome`/`Resync`. Из тестов поменялся только `codec_test` (поля переехали в `CatchUp`).

2. **`f/net-drop-hold-prediction`** (смена поведения, нужно решение): убрать `RemoteInputPrediction::Hold` — по замерам (`remote-sync-research.md`) он хуже `Neutral`. Уходят поле настройки, ветки `if (neutral)` в `TrySendPendingCommands`, проверка в `ConfirmInputThrough`, тесты на `Hold`. Минус: пропадает A/B-сравнение в `sync_bench_test`; если оно нужно, оставить как параметр теста, а не настройки.

   Сделано (решение: убрать совсем, и из бенчмарка тоже). Тест «поздний вход посреди удержания» мерил ровную скорость, которая держалась только при `Hold`; теперь он сверяет вид наблюдателя с сервером после отпускания клавиши и проверяет, что ресинка не было. Этап 3 — ввод на время ожидания `Resync` копится и уходит одним батчем на текущий тик (решение от 2026-10-07).

3. **`f/net-resync-wait`** (смена поведения, нужно решение): упростить ожидание `Resync`. Сейчас ради него есть `OutgoingCommands.applied_count`, `CollapseHeldBackRecords`, `ScheduleLocally`, пустые heartbeat-батчи и две версии `ApplyOwnCommands` (`player_action_recorder.cpp` для одиночной игры, `net_action_sender.cpp` для сети). Вариант: на время ожидания ввод не придерживается и не ретаймится, а отбрасывается (или шлётся одним батчем после `Resync`). Цена — возможная потеря ввода в редком случае. Заодно свести две `ApplyOwnCommands` в одну. Сначала тест на сценарий (ввод во время ожидания `Resync`), он же закрывает техдолг «пульс после `ResyncRequest` не проверен».

   Сделано, без потери ввода (решение от 2026-10-09: свой ввод передаётся весь, не схлопывается). Сервер собирает `Resync`, как только обрабатывает `ResyncRequest`, а команды идут тем же упорядоченным reliable-каналом, так что всё отправленное после запроса в `Resync` не попадает. Поэтому на время ожидания ввод применяется и шлётся как обычно, отправленное копится в `StateDigests.sent_since_resync_request`, и `AdoptCatchUp` вливает его вместе с ещё не отправленными `OutgoingCommands` в принятый журнал. Своя запись применяется в свой тик одной `ApplyOwnCommands` (`player_action_recorder.cpp`, во всех ролях). Ушли `applied_count`, `CollapseHeldBackRecords`, `ScheduleLocally` и отдельная ветка пульса (пульс шлёт обычный путь, пока удерживается отпускаемое действие). Тесты: `EveryInputWhileAwaitingAResyncIsApplied`, `ReleaseUnsentWhenAResyncStartsSurvivesIt`, `HeartbeatsWhileAResyncIsInFlightKeepConfirmingInput`.

4. **`f/net-tuning-trim`** (мелкая): уплинк-бюджеты (`client_uplink_budget_*`, `idle_uplink_budget_*`) — в константы `UplinkBudgetTest`; `max_actions_per_client`/`max_remote_actions` — в `constexpr`; пересмотреть, что из настроек реально нужно слать в `Welcome` (для одинакового тика нужны `fps` и физика). Три порога часов оставить, но проверить, нужен ли каждый.

## Отложено до замеров

- **Отложенные откаты** (`DeferRollback`, `max_deferred_rollbacks`, `max_rollback_delay_ticks`, `DeferredRollbackTick` в трёх потребителях). Идея «не откатываться, если подтверждение совпало с предсказанием» (`action-delivery-plan.md`, открытые вопросы) могла бы убрать большую часть откатов и сделать пакетирование лишним. Сначала сделать её и замерить, потом решать, нужен ли `DeferRollback`.
- **Счётчики дайджестов** (`checked`, `resyncs`, `last_sent_tick`): часть нужна только тестам. Сверку убирать нельзя — это единственная защита от дрейфа.

## Критерии

- Этап 1: все существующие тесты сети проходят без правок, кроме замены `Result`; `PluginSmoke` и `check_duplicated_state.py` зелёные.
- Этапы 2–3: главный инвариант (полный `WorldJsonStore::Save` сервера и клиентов совпадает), `UplinkBudgetTest`, `desync_test`, `paused_peer_test`.
- Суммарный diff каждой ветки в пределах ~1000 строк (CLAUDE.md); этап 1 из-за переносов кода может быть больше — флагнуть при ревью.
