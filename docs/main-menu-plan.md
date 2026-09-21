# План: главное меню

## Контекст

Сейчас zodiac13 стартует сразу в геймплей. Нужно: (1) главное меню перед стартом игры
(Start Game / Settings / Exit), (2) кнопка выхода из геймплея обратно в меню, и (3) режим
запуска, при котором меню вообще пропускается и сцена грузится сразу — это должно идти через
уже существующий, но сейчас фактически неработающий `bootstrap`-модуль. Решения, принятые по
ходу обсуждения:

- "Выйти в главное меню" **уничтожает всю игровую сцену** (игрок, камера, блоки построек) —
  не просто пауза. Сам flecs `world` не пересоздаётся — это постоянный контейнер всего
  приложения (и меню, и геймплей — просто разные сущности в одном мире).
- До нажатия "Start Game" сцены не существует вообще.
- Текст интерфейса — на английском (ImGui по умолчанию не умеет в кириллицу, шрифт не
  добавляем).
- Существующий класс `MainMenuWindow` (сейчас он на самом деле — меню паузы) переименовывается
  в `GameplayPauseMenuWindow`. Новый класс `MainMenuWindow` — это настоящий стартовый экран.
- Должен быть режим "запустить сразу без меню", выбираемый снаружи (флаг), и решение о том,
  показывать меню или сразу грузить сцену, должно приниматься в `bootstrap`-модуле, а не в
  GUI/z13_module напрямую.

## Важная находка: bootstrap сейчас не работает

`BootstrapSystem::Register` (`bootstrap_system.cpp`) регистрирует систему `"InitBootstrap"`,
которая должна проставлять `LoadConfigEvent`/`CreatePlayerEvent` (позже переименован в
`SelectInitialStateEvent`) на служебную сущность с тегом
`BootstrapComponent`. Но функция `RegisterComponents`, которая создаёт эту сущность
(`world.entity().add<BootstrapComponent>()`), нигде не вызывается — она мертва. Соответственно
`InitBootstrap` никогда не находит подходящую сущность, `LoadConfigEvent`/`CreatePlayerEvent`
никогда не выставляются. Сцена сейчас реально спавнится через `z13_module.cpp`'s
`OnCreateDefaults` (`InitWorldDataEvent`-обработчик), который безусловно делает
`world.add<gameplay::Gameplay>()`, что триггерит `OnAdd`-обсервер в `gameplay_system.cpp`
(`GameplaySystem::OnInit`), вызывающий `CreateTestPlayer`.

План ниже это чинит: bootstrap становится реальной точкой принятия решения "показывать меню или
сразу стартовать", как и предполагал `todo`-комментарий в файле.

## Дизайн

### Состояние: те же теги `Gameplay`/`Pause`, без новых компонентов состояния сцены

| Состояние | `Gameplay` | `Pause` | Что показано |
|---|---|---|---|
| Стартовый экран (сцены нет) | нет | есть | `MainMenuWindow` |
| Играем | есть | нет | ничего |
| Пауза в игре | есть | есть | `GameplayPauseMenuWindow` |

`z13::gameplay::Gameplay` меняет смысл с "выставлен один раз при старте, никогда не снимается"
на "сейчас есть активная игровая сцена" — `add<Gameplay>()` порождает сцену (уже работает через
существующий `OnAdd`-обсервер → `CreateTestPlayer`), `remove<Gameplay>()` уничтожает её (новый
`OnRemove`-обсервер, ниже). Он и так `Singleton`, не `State` — то есть не переживает
quicksave/quickload, что и нужно для сессионного флага.

### Кто решает Pause vs Gameplay при старте — bootstrap

Флаг берётся из `Config` (`Config::SkipMainMenu()`) через `z13::GetCoreConfig(world)`
(`flecs_utils.h`), отдельного компонента для него нет: `Config` разбирается в конструкторе `Core`,
то есть к первому кадру уже готов.

`bootstrap_system.cpp` чинится и дополняется:

```cpp
void RegisterComponents(flecs::world world) {
  world.entity().add<BootstrapComponent>();
  z13::flecs_tools::RegisterComponent<BootstrapCompleteComponent>(world);
}

void OnLoadConfig(flecs::entity e, const LoadConfigEvent&) {
  e.remove<LoadConfigEvent>();  // пока просто закрывает шаг; реальная загрузка конфига уже есть в InputConfigLoader
}

void OnSelectInitialState(flecs::entity e, const SelectInitialStateEvent&) {
  flecs::world world = e.world();
  const auto config = z13::GetCoreConfig(world);
  if (config && config->get().SkipMainMenu()) {
    world.add<gameplay::Gameplay>();
  } else {
    world.add<gameplay::Pause>();
  }
  e.remove<SelectInitialStateEvent>();
}

void InitBootstrap(flecs::entity e, const BootstrapComponent&) {
  e.add<LoadConfigEvent>();
  e.add<SelectInitialStateEvent>();
  e.world().add<BootstrapCompleteComponent>();
}
```

```cpp
void BootstrapSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("BootstrapSystem::RegisterComponents")
      .event(flecs::OnAdd).yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  // Системы — только после регистрации компонентов: `without<BootstrapCompleteComponent>()`
  // создаёт запрос, а трейт Singleton нельзя навесить на уже опрошенный компонент.
  world.observer<InitSystemsEvent>("BootstrapSystem::RegisterSystems")
      .event(flecs::OnAdd).yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}
```
`RegisterSystems` создаёт обсерверы `LoadConfigEvent`/`SelectInitialStateEvent` и систему `InitBootstrap`.

`z13_module.cpp`'s `OnCreateDefaults`: убрать `world.add<z13::gameplay::Gameplay>();` — решение
"Pause или Gameplay" теперь целиком за bootstrap. Заодно можно удалить мёртвую неиспользуемую
функцию `CreateDefaults` в том же файле (она нигде не вызывается) как попутную чистку.

Важно: `InitBootstrap` — это система на фазе `PreUpdatePhase`, она выполняется внутри
`world.progress()`, то есть на первом кадре `Core::Run()`, а не синхронно внутри
`Core::CreateWorld()`.

### Как флаг попадает в мир: CLI-опция через существующий `Config`

В `Config`/`config.cpp` уже есть `boost::program_options` (сейчас только `--help`). Добавляем:

```cpp
// config.cpp, Config::Config()
options_description_.add_options()
    ("help,h", "Show help message")
    ("skip-main-menu", po::bool_switch(&skip_main_menu_), "Start gameplay immediately, skip the main menu");
...
po::store(po::parse_command_line(...), variables_map_);
po::notify(variables_map_);
```
`Config::SkipMainMenu() const { return skip_main_menu_; }`, плюс поле `bool skip_main_menu_ {false};`.

Путь быстрого сохранения тоже идёт через `Config`: опция `--quick-save-path`,
`Config::GetQuickSavePath()` (без неё — путь по умолчанию из `GetGameQuickSaveJsonPath()`).
`Z13ModuleFactory` читает его через `GetCoreConfig`, `SetQuickSavePath` не нужен.

`z13_launcher.cpp` и `ModuleFactoryBase`/dlopen-загрузка модулей не меняются.

`Z13TestWorld` получает тот же эффект, собирая `Core` из настоящего argv (см. "Тесты").

### Уничтожение сцены при "Exit to Main Menu" — без изменений в подходе

Как и раньше: `OnRemove`-обсервер на `Gameplay` в `gameplay_system.cpp`, уничтожающий все
сущности с тегом `flecs_tools::StateEntity` (игрок и каждый блок уже им помечены):

```cpp
// Синглтон: each(entity, ...) не подходит (нет entities), нужна сигнатура с flecs::iter, как у OnInit.
void OnTeardown(flecs::iter it, size_t, const gameplay::Gameplay&) {
  it.world().query_builder().with<flecs_tools::StateEntity>().build()
      .each([](flecs::entity e) { e.destruct(); });
}

world.observer<gameplay::Gameplay>("GameplaySystem::OnTeardown")
    .event(flecs::OnRemove)
    .each(OnTeardown);
```

Проверено: `Brush`-сущности — дети игрока (`child_of`), удаляются каскадно бесплатно.
Побочные таблицы в bullet_module (`PhysicsWorld::bodies`) и raylib_module (`BlockModels`) сами
чистят мёртвые id каждый кадр (`ReleaseOrphanBodies`/`ReleaseOrphanModels`) — явной очистки не
требуется. `IdCounters` уже пересобирается с нуля в `OnInit` при каждом новом `add<Gameplay>()`.

### GUI: два класса окна вместо одного

`gui_windows.h/.cpp`:

- Переименовать `MainMenuWindow` → `GameplayPauseMenuWindow` (фабрика `MakeGameplayPauseMenu`).
  Кнопка "Resume" — как раньше (`RequestCloseMenu()`), "Settings..." — без изменений. Кнопку
  "Exit" заменить на **"Exit to Main Menu"**: `World().remove<gameplay::Gameplay>();
  RequestPop();` — снимает себя со стека; `Pause` остаётся, поэтому на следующем кадре
  `GuiSystem::Draw` увидит пустой стек + паузу + отсутствие `Gameplay` и покажет новый
  `MainMenuWindow`.
- Добавить новый класс `MainMenuWindow` (фабрика `MakeMainMenu`) — стартовый экран:
  - "Start Game": `World().add<gameplay::Gameplay>(); RequestCloseMenu();`
  - "Settings...": как в паузе, пуш `InputSettingsWindow`.
  - "Exit": `World().add<RaylibWindowClosed>();` — как сейчас работает выход из приложения.
  - `OnBack()` — переопределить как no-op (`return {};`): на стартовом экране Esc не должен
    "продолжать" в пустой мир.

`gui_system.cpp`, `GuiSystem::Draw` — выбор класса окна при показе меню:

```cpp
if (world.has<gameplay::Pause>() && stack.windows.empty()) {
  if (world.has<gameplay::Gameplay>()) {
    stack.windows.push_back(gui::MakeGameplayPauseMenu(world));
  } else {
    stack.windows.push_back(gui::MakeMainMenu(world));
  }
}
```

Остальная логика `Draw` (очистка стека при снятии паузы, отрисовка верхнего окна,
`InputSettingsWindow`/`KeyBindingsWindow`) не меняется.

## Файлы к изменению

- `src/lib_core/include/lib_core/config.h`, `src/lib_core/src/config.cpp` — опции
  `--skip-main-menu` и `--quick-save-path`, методы `SkipMainMenu()`/`GetQuickSavePath()`.
- `src/lib_core/include/lib_core/flecs_utils.h`, `src/lib_core/src/flecs_utils.cpp` —
  `GetCoreConfig(world)`.
- `src/lib_z13/components/include/z13/components/bootstrap.h` — событие
  `SelectInitialStateEvent`.
- `src/lib_z13/modules/z13_module/src/bootstrap/bootstrap_system.cpp` — починить
  `RegisterComponents` (подключить к `RegisterComponentsEvent`), реализовать
  `OnLoadConfig`/`OnSelectInitialState` вместо мёртвых тегов.
- `src/lib_z13/modules/z13_module/src/z13_module.cpp` — убрать `add<Gameplay>()` из
  `OnCreateDefaults`; опционально убрать мёртвую `CreateDefaults`.
- `src/lib_z13/modules/z13_module/src/gameplay/gameplay_system.cpp` — добавить `OnRemove`
  teardown-обсервер на `Gameplay`.
- `src/lib_raylib_module/src/gui/gui_windows.h`/`.cpp` — переименование +
  новый класс `MainMenuWindow`.
- `src/lib_raylib_module/src/gui/gui_system.cpp` — выбор окна по `has<Gameplay>()`.
- `src/lib_z13/modules/z13_module/tests/support/z13_test_world.h` — см. ниже.

Новых типов компонентов не появляется. Новых файлов — один тестовый.

## Тесты

`Z13TestWorld` раньше получал игрока синхронно внутри `CreateWorld()` (через безусловный
`add<Gameplay>()`). После починки bootstrap спавн происходит через систему на
`PreUpdatePhase`, которую harness запускает один раз вручную (`progress()` не подходит — он сдвигает счётчики тиков). Чтобы не трогать ~11
существующих тестовых файлов, конструктор делает это прозрачно:

```cpp
explicit Z13TestWorld(bool skip_main_menu = true)
    : core_(MakeCore(skip_main_menu, quick_save_path_)), world_(CreateWorld(core_)) {}

void StartGame() { World().add<z13::gameplay::Gameplay>(); }
void ExitToMainMenu() { World().remove<z13::gameplay::Gameplay>(); }

// MakeCore(...): Core собирается из argv с --skip-main-menu / --quick-save-path=...,
// так что модуль читает те же настройки из Config, что и в игре.
// CreateWorld(...): после core.CreateWorld() —
ecs_run(world, InitBootstrap, 0, nullptr);  // разовый запуск системы без кадра: тесты,
                                            // считающие тики (снимки, лог действий), не сдвигаются
```

По умолчанию (`skip_main_menu = true`) поведение для существующих тестов не меняется — игрок
доступен сразу после конструктора. Новые тесты, которым нужен "чистый экран меню", создают
`Z13TestWorld w(false);`.

Новый файл `src/lib_z13/modules/z13_module/tests/gameplay/main_menu_test.cpp`:

- `SkipMainMenuStartsWithSceneImmediately` — `Z13TestWorld w(true)` (по умолчанию): игрок есть,
  `Gameplay` есть, `Pause` нет.
- `NormalLaunchStartsAtMainMenu` — `Z13TestWorld w(false)`: игрока нет, `Gameplay` нет, `Pause`
  есть.
- `StartGameSpawnsPlayer` — из состояния меню вызвать `StartGame()`, игрок появляется.
- `ExitToMainMenuDestroysPlayerAndBlocks` — построить блок, `ExitToMainMenu()`, игрок и блок
  исчезают.
- `ExitToMainMenuThenStartGameGetsFreshIdCounters` — выйти, зайти заново, построить блок,
  убедиться что id снова начинается с 1.

Самоочистку `PhysicsWorld`/`BlockModels` стоит проверить одной ассерцией в существующем тесте
bullet_module (после teardown — `BodyCount() == 0`), не строя отдельную инфраструктуру.
GUI/ImGui-кнопки без headless-тестов — проверяются вручную (см. ниже).

## Верификация

1. Сборка: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j3` (под
   PRoot — не больше `-j3`).
2. `ctest --test-dir build` — все текущие тесты проходят без изменений + новые из
   `main_menu_test.cpp`.
3. Ручная проверка:
   - Обычный запуск (без флага) → стартовый экран на пустом мире, "Start Game" запускает игру.
   - Esc в игре → пауза с "Resume"/"Settings..."/"Exit to Main Menu".
   - "Exit to Main Menu" → возврат на стартовый экран, сцена реально уничтожена (полетать,
     убедиться что старых блоков/игрока нет).
   - "Start Game" повторно → свежая сцена, первый блок снова `Block_1`.
   - "Exit" на стартовом экране → приложение закрывается.
   - Esc на стартовом экране → ничего не происходит.
   - Запуск с `--skip-main-menu` → игра стартует сразу, без меню, сцена присутствует с первого
     кадра.
