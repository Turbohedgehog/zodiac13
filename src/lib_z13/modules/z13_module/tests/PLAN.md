# План: интеграционные тесты input-пайплайна (без SDL/raylib)

## 0. Цель

Проверить весь путь ввода end-to-end: `flecs::world` → `z13_module` →
события (`MouseMoveEvent`/`KeyboardDownEvent`/...) → `InputState` →
`ActionListener` → `ApplyCameraMove` → transform игрока. Без реального SDL/raylib
окна — эмулируем события напрямую.

Подтверждено разведкой: `z13_module` не имеет ни compile-, ни link-зависимости
от raylib/SDL (`src/lib_z13/modules/z13_module/CMakeLists.txt`, только
`Eigen3::Eigen PUBLIC`, `zodiac13::core`/`zodiac13::components PRIVATE`,
`sago::platform_folders`, `Boost::dll`). `raylib_module` реально поднимает
`SDL_Init(SDL_INIT_VIDEO)` + `SDL_CreateWindow` — участвовать в тесте не должен
и не нужен.

## 1. Переиспользование кода — три точки дублирования

При наивной реализации тест дублировал бы код, который уже есть в проде (или
который продублируют будущие тесты). Нашлись три конкретных места:

1. **Эмуляция события ввода.** Паттерн "положить компонент на entity, затем
   `world.event<SystemInputEventType>().id<EventT>().entity(source).emit()`"
   уже существует в проде как приватный шаблон `Emit<EventT>`
   (`src/lib_raylib_module/src/tools/input_publisher.cpp:157-161`, анонимное
   пространство имён). Тест не должен зависеть от `raylib_module` (там
   SDL/GL), поэтому копировать эту функцию в тест — дублирование логики,
   которая обязана остаться идентичной проду. Решение — поднять её на уровень
   выше, туда, где и так лежат типы событий, которыми она оперирует.
2. **Bootstrap headless-мира.** `Core` + `Z13ModuleFactory` +
   `SetPersistInputConfigDefaults(false)` + `CreateWorld()` — если не
   вынести, каждый следующий интеграционный тест в `z13_module` (не только
   input, в будущем — building/gameplay) будет копировать этот бойлерплейт
   заново.
3. **Имя тестового игрока.** `"TestPlayer"` сейчас голый строковый литерал в
   `src/lib_z13/modules/z13_module/src/gameplay/gameplay_system.cpp:58`,
   который тест обязан был бы продублировать при поиске сущности. Нужен один
   источник истины.

Как каждая решается — в соответствующих разделах ниже (2-я точка — в п.4,
3-я — в п.5, 1-я — в п.6).

## 2. Bootstrap мира (без raylib)

```cpp
z13::Core core(0, nullptr);
auto factory = std::make_shared<z13::Z13ModuleFactory>();
factory->SetPersistInputConfigDefaults(false);  // см. п.3 — настройка модуля, не Core
core.RegisterModuleFactory(factory);
flecs::world& world = core.CreateWorld().get();
```
`Core` здесь не меняется — используются только уже существующие
`RegisterModuleFactory`/`CreateWorld`. `SetPersistInputConfigDefaults` — новый
метод на `Z13ModuleFactory` (модуль), не на `Core`.

`Core::CreateWorld()` (`src/lib_core/src/core.cpp:67-85`) сам вызывает
`RegisterComponentsEvent → InitPhasesEvent → InitSystemsEvent →
InitWorldDataEvent`, что через `OnCreateDefaults → OnStartupGameEvent →
OnInputSystemStartupGameEvent` создаёт полностью укомплектованную сущность
`"TestPlayer"` (Camera, transform, `ActionListener`, `CurrentActionListenerTag`,
`InputListener`) — собирать её вручную не требуется
(`src/lib_z13/modules/z13_module/src/gameplay/gameplay_system.cpp:57-81`,
`CreateTestPlayer`).

Этот блок целиком выносится в общий тестовый фикстур — см. п.4.

## 3. Настройка: не писать input-config на диск в тестах

**Проблема:** на первом прогоне `OnInputSystemStartupGameEvent`
(`src/lib_z13/modules/z13_module/src/input/gameplay_input_system.cpp:289-311`)
делает `LoadConfig` → если файла нет, `SetDefaults` (in-memory, из
скомпилированной flatbuffer-схемы, диск не нужен) → `SaveConfig` (реальная
запись в `sago::getDataHome()/Zodiac13`, побочный эффект, не требование).
Точки override для `GetGameDataDirectory()` в коде нет. Синглтон `InputConfig`
создаётся и тут же сохраняется двумя соседними строками одной синхронной
цепочки `Core::CreateWorld()` (`z13_module.cpp:59-60`, `OnCreateDefaults`) —
предзаполнить его между этими строками тест не может.

**Решение — без изменений в `Core`.** `Core` не должен ничего знать про
input-config — это забота модуля. Точка переопределения — сама
`Z13ModuleFactory`: её `RegisterModules(world)` (`z13_module_factory.cpp:29-31`)
вызывается **до** `world.add<RegisterComponentsEvent>()`/`InitWorldDataEvent`
внутри `Core::CreateWorld()` (`core.cpp:75-82`) — то есть ровно в нужном окне,
и это уже код модуля, а не ядра.

- **Новый компонент** `z13::input::InputConfigPersistenceSettings` —
  `src/lib_z13/components/include/z13/components/input.h`, рядом с
  `InputConfig`:
  ```cpp
  struct InputConfigPersistenceSettings {
    bool persist_defaults_to_disk {true};
  };
  ```

- **Регистрация синглтона** — `z13_module.cpp`:
  - `OnRegisterComponents` (рядом со строкой 48):
    `world.component<input::InputConfigPersistenceSettings>().add(flecs::Singleton);`
  - `OnCreateDefaults` (рядом со строкой 59):
    `world.add<input::InputConfigPersistenceSettings>();` — safety-net дефолт
    (`persist_defaults_to_disk = true`) на случай импорта `Z13Module` в обход
    фабрики; `add<T>()` не перезаписывает значение, если фабрика уже
    выставила своё раньше.

- **Переопределение — в самой фабрике**, `z13_module_factory.h`/`.cpp`:
  ```cpp
  // .h
  void SetPersistInputConfigDefaults(bool persist);
  // private: bool persist_input_config_defaults_ {true};

  // .cpp
  void Z13ModuleFactory::RegisterModules(flecs::world& world) {
    world.import<Z13Module>();
    world.set<z13::input::InputConfigPersistenceSettings>(
        {.persist_defaults_to_disk = persist_input_config_defaults_});
  }
  ```

- **Использование** — `gameplay_input_system.cpp::OnInputSystemStartupGameEvent`
  (строки 289-313) и его регистрация (строка 428) получают синглтон четвёртым
  параметром запроса:
  ```cpp
  void OnInputSystemStartupGameEvent(
      flecs::iter it,
      size_t,
      z13::input::InputConfig& input_config,
      z13::input::ActionMap& action_map,
      const z13::input::InputConfigPersistenceSettings& persistence,
      status::OnStartupGameEvent) {
    ...
    if (!InputConfigLoader::LoadConfig(input_config, action_map)) {
      InputConfigLoader::SetDefaults(input_config, action_map);
      if (persistence.persist_defaults_to_disk) {
        InputConfigLoader::SaveConfig(input_config, action_map);
      }
    }
    ...
  }
  ```
  ```cpp
  world.observer<z13::input::InputConfig, z13::input::ActionMap,
      z13::input::InputConfigPersistenceSettings, z13::status::OnStartupGameEvent>(
      "gameplay_input_system::OnStartupGameEvent")
      .event(flecs::OnAdd)
      .yield_existing()
      .each(OnInputSystemStartupGameEvent);
  ```

Прод-поведение не меняется (дефолт `true` — и в самой фабрике, и в
safety-net `OnCreateDefaults`).

## 4. Общий тестовый фикстур (переиспользование между тестами)

Вместо повторения п.2 в каждом `TEST`/`TEST_F` — headless-фикстур,
переиспользуемый любыми будущими интеграционными тестами `z13_module`, не
только этим. Живёт **только** в тестах (не в публичном `include/` модуля —
это не часть API движка, а тестовая обвязка):

`src/lib_z13/modules/z13_module/tests/support/z13_test_world.h`
(header-only, без .cpp — как и `camera_look.h`/`.cpp`-пара, но здесь всё
влезает в заголовок):
```cpp
#pragma once

#include <memory>

#include <flecs.h>

#include <lib_core/core.h>
#include <z13_module/z13_module_factory.h>

#include <z13/components/input_event_emitter.h>  // см. п.6
#include <z13/components/gameplay.h>              // kTestPlayerEntityName, см. п.5

namespace z13::testing {

constexpr std::string_view kTestInputSourceName = "Z13TestWorld::InputSource";

// Headless z13::Core + z13_module world: без raylib/SDL, без записи
// input-config на диск (см. PLAN.md §3). Общий для всех интеграционных
// тестов z13_module.
class Z13TestWorld {
 public:
  Z13TestWorld() : world_(CreateWorld(core_)) {}

  flecs::world& World() { return world_.get(); }

  flecs::entity Player() {
    return World().lookup(z13::gameplay::kTestPlayerEntityName.data());
  }

  flecs::entity InputSource() {
    return World().entity(kTestInputSourceName.data());
  }

  template <typename EventT>
  void EmitInput(const EventT& event) {
    z13::input::EmitInputEvent(World(), InputSource(), event);
  }

 private:
  static z13::WorldRef CreateWorld(z13::Core& core) {
    auto factory = std::make_shared<z13::Z13ModuleFactory>();
    factory->SetPersistInputConfigDefaults(false);
    core.RegisterModuleFactory(factory);
    return core.CreateWorld();
  }

  z13::Core core_ {0, nullptr};
  z13::WorldRef world_;  // объявлен после core_ — важен порядок инициализации
};

}  // namespace z13::testing
```

Использование в конкретном тесте:
```cpp
TEST(InputPipeline, MouseLookRotatesCamera) {
  z13::testing::Z13TestWorld test_world;
  test_world.EmitInput(z13::input::MouseMoveEvent{.delta = {10, 0}});
  test_world.World().progress(kDeltaTime);
  // ... проверка test_world.Player()
}
```

## 5. Общее имя тестового игрока

`src/lib_z13/components/include/z13/components/gameplay.h` (уже публичный,
уже включается и продом, и тестами) — добавить рядом с существующими
структурами:
```cpp
#include <string_view>
...
constexpr std::string_view kTestPlayerEntityName = "TestPlayer";
```
`gameplay_system.cpp::CreateTestPlayer` (строка 58) переключается на
`world.entity(kTestPlayerEntityName.data())` вместо голого литерала — один
источник истины для продовой сборки сущности и тестового поиска
(`Z13TestWorld::Player()` из п.4). Заодно приводит существующий код к стилю
CLAUDE.md (именованные строковые константы вместо литералов).

## 6. Общий emit-хелпер для событий ввода (переиспользование прод/тест)

**Проблема с местом.** `raylib_module` и `z13_module` — независимые
sibling-модули без зависимости друг на друга (plugin-архитектура через
`boost::dll`, подтверждено — `raylib_module` нигде не включает
`z13_module/...`). Класть общий хелпер в `z13_module` означало бы заставить
`raylib_module` слинковаться с `z13_module` — новая архитектурная связь ради
одной функции, отдельно ломающая независимую загружаемость модулей.
`zodiac13::core` тоже не годится: он ничего не знает про
`z13::input::SystemInputEventType` (тип из `zodiac13::components`, а `core`
её не линкует).

**Решение:** `zodiac13::components` — там уже лежат `SystemInputEventType`,
`MouseMoveEvent` и другие типы, которыми оперирует хелпер, и её и так уже
линкуют `raylib_module`, `z13_module` и тестовый таргет. Единственное, чего
там пока нет — flecs, но это чисто техническое ограничение (текущий
CMakeLists просто не заводил эту зависимость), а не архитектурный принцип:
сами эти типы существуют только ради flecs-эмита.

- `src/lib_z13/components/CMakeLists.txt` — добавить
  `find_package(flecs CONFIG REQUIRED)` и `flecs::flecs_static` в
  `target_link_libraries(... INTERFACE ...)`. Новых рёбер графа зависимостей
  не возникает — только у `components` появляется одна declared-зависимость,
  которая раньше была скрытой (типы всё равно нигде не использовались без
  flecs).

- **Новый заголовок**
  `src/lib_z13/components/include/z13/components/input_event_emitter.h`:
  ```cpp
  #pragma once

  #include <flecs.h>

  #include <z13/components/input.h>

  namespace z13::input {

  // Кладёт EventT как payload-компонент на source и эмитит его как
  // SystemInputEventType-событие — общий код между реальным input-слоем
  // (raylib_module::InputPublisher) и тестами, эмулирующими ввод без
  // SDL/raylib.
  template <typename EventT>
  void EmitInputEvent(flecs::world world, flecs::entity source, const EventT& event) {
    source.set<EventT>(event);
    world.event<SystemInputEventType>().id<EventT>().entity(source).emit();
  }

  }  // namespace z13::input
  ```

- **Прод меняется, не только тест:**
  `src/lib_raylib_module/src/tools/input_publisher.cpp` — удалить приватный
  `Emit<EventT>` (строки 157-161), подключить новый заголовок, все вызовы
  `Emit(world, source, event)` заменить на `z13::input::EmitInputEvent(world,
  source, event)`. Одна реализация вместо двух — гарантия, что тест реально
  бьёт по тому же коду, что и прод, а не по своей копии, которая могла бы
  разъехаться с оригиналом.

## 7. Расположение и сборка

Новые/изменённые файлы:
- `src/lib_z13/components/include/z13/components/input_event_emitter.h` (новый)
- `src/lib_z13/components/include/z13/components/input.h` (компонент из п.3)
- `src/lib_z13/components/include/z13/components/gameplay.h` (константа из п.5)
- `src/lib_z13/components/CMakeLists.txt` (flecs-зависимость из п.6)
- `src/lib_z13/modules/z13_module/src/z13_module.cpp` (п.3)
- `src/lib_z13/modules/z13_module/include/z13_module/z13_module_factory.h` /
  `src/z13_module_factory.cpp` (п.3)
- `src/lib_z13/modules/z13_module/src/input/gameplay_input_system.cpp` (п.3),
  `src/gameplay/gameplay_system.cpp` (п.5)
- `src/lib_raylib_module/src/tools/input_publisher.cpp` (п.6, прод-рефактор)
- `src/lib_z13/modules/z13_module/tests/support/z13_test_world.h` (новый, п.4)
- `src/lib_z13/modules/z13_module/tests/input/input_pipeline_test.cpp` (новый)

`input_pipeline_test.cpp` подхватится автоматически существующим glob'ом в
`src/tests/CMakeLists.txt`
(`${CMAKE_SOURCE_DIR}/src/lib_z13/modules/*/tests/*.cpp`) — правки CMake для
самого теста не нужны, линковка с `zodiac13::z13_module` уже есть.
`z13_test_world.h` в `tests/support/` подхватывать не нужно — это заголовок,
не `.cpp`, попадает в сборку через `#include` из `input_pipeline_test.cpp`.

Прецедент — `src/lib_z13/modules/z13_module/tests/gameplay/camera_look_test.cpp`
(но тот тестирует только чистую функцию `ApplyCameraMove`, без `flecs::world`;
текущий тест — первый, который импортирует модуль в мир).

## 8. Тестовые сценарии — интеграционный код (не пересказ, а реализация)

Все сценарии идут через реальный пайплайн (`Z13TestWorld` из п.4): событие →
observer → `InputState` → `CalculateInputValues` → `ApplyMoveActionListener` →
`ApplyCameraMove`. Числа не произвольные — выведены из фактической семантики
`ActionValueHolder`/фаз (`z13/components/input.h:224-262`):
`ClearActionFramePhase` каждый кадр обнуляет `current_value` (сохраняя его в
`prev_value`), `CalculateActionFramePhase` затем прибавляет к уже обнулённому
значению дельту этого кадра — то есть для зажатой клавиши это плоское
"1.0 каждый кадр, пока не пришёл `KeyboardUpEvent`", а для мыши — "дельта
только в кадре, где было событие, дальше 0". Дефолты: `mouse_sensitivity =
5.f`, `invert_x = invert_y = false` (`input.h:206-207`, `SetDefaults` их не
трогает), `kCameraVelocity = 30.f` (`camera_look.h`), forward по умолчанию
забинжен на `KEY_W` (`actions.fbs`).

```cpp
#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/gameplay/camera_look.h>

#include "support/z13_test_world.h"

namespace z13::gameplay::input {
namespace {

Eigen::Vector3f Position(flecs::entity player) {
  return player.get<Eigen::Matrix4f>().col(3).head<3>();
}

z13::input::KeyboardDownEvent KeyDown(z13::fbs::input::Keycode code) {
  z13::input::KeyboardDownEvent event;
  event.keycode.code = code;
  return event;
}

z13::input::KeyboardUpEvent KeyUp(z13::fbs::input::Keycode code) {
  z13::input::KeyboardUpEvent event;
  event.keycode.code = code;
  return event;
}

// Событие мышью попадает в InputState только в том кадре, где оно было
// эмичено; dt=0.01 подобран так, чтобы -delta.x * dt * mouse_sensitivity
// давал круглое число (100 * 0.01 * 5 = 5).
TEST(InputPipelineTest, MouseLookRotatesCameraThroughPipeline) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();
  ASSERT_TRUE(player.is_alive());

  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = 100, .y = 0};
  test_world.EmitInput(move_event);

  test_world.World().progress(0.01f);

  ASSERT_TRUE(player.has<z13::gameplay::LookAngles>());
  // invert_x=false, delta.x=100 > 0 (мышь вправо) -> yaw_deg отрицательный.
  EXPECT_NEAR(player.get<z13::gameplay::LookAngles>().yaw_deg, -5.f, 1e-3f);
  EXPECT_TRUE(Position(player).isZero(1e-4f));  // поворот не должен двигать позицию
}

// Клавиша держится через несколько кадров -> позиция копится линейно;
// KeyboardUpEvent останавливает движение.
TEST(InputPipelineTest, HeldForwardKeyMovesPositionUntilKeyUp) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();

  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));

  test_world.World().progress(1.f);
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));

  test_world.World().progress(1.f);  // событие не повторяли -- клавиша всё ещё held
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(2.f * z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));

  test_world.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_W));
  test_world.World().progress(1.f);
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(2.f * z13::gameplay::kCameraVelocity, 0.f, 0.f), 1e-3f));
}

// Аналог camera_look_test.cpp::ForwardMoveFollowsCameraAfterTurning, но через
// реальные события, а не прямой вызов ApplyCameraMove. delta.x=-18, dt=1 ->
// -(-18) * 1 * 5 = 90 -- поворот ровно на 90 градусов за один кадр.
TEST(InputPipelineTest, ForwardMoveFollowsCameraAfterMouseTurnThroughPipeline) {
  z13::testing::Z13TestWorld test_world;
  auto player = test_world.Player();

  z13::input::MouseMoveEvent turn_event;
  turn_event.delta = {.x = -18, .y = 0};
  test_world.EmitInput(turn_event);
  test_world.World().progress(1.f);
  ASSERT_NEAR(player.get<z13::gameplay::LookAngles>().yaw_deg, 90.f, 1e-3f);

  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_W));
  test_world.World().progress(1.f);

  // Поворот на +90 yaw переводит forward-ось камеры из +X в +Y.
  EXPECT_TRUE(Position(player).isApprox(
      Eigen::Vector3f(0.f, z13::gameplay::kCameraVelocity, 0.f), 1e-3f));
}

}  // namespace
}  // namespace z13::gameplay::input
```

**Порядок фаз** проверяется неявно каждым из трёх тестов выше: если
`ClearActionFramePhase`/`CalculateActionFramePhase`/`ApplyActionFramePhase`
отработают не по порядку за один `world.progress()`, ожидаемые числа не
сойдутся — отдельного теста на порядок фаз не требуется.

Примечание: `kCameraVelocity` сейчас объявлен в `namespace z13::gameplay` в
`camera_look.h` — используется как есть, дублирования нет.

## 9. Порядок реализации

1. ✅ `InputConfigPersistenceSettings` в `z13/components/input.h` (п.3).
2. ✅ Регистрация синглтона в `z13_module.cpp` (`OnRegisterComponents` +
   safety-net в `OnCreateDefaults`, п.3).
3. ✅ Сеттер `Z13ModuleFactory::SetPersistInputConfigDefaults` +
   `world.set<...>()` в `RegisterModules` (п.3).
4. ✅ Правка `OnInputSystemStartupGameEvent` и её регистрации (п.3).
5. ✅ `kTestPlayerEntityName` в `gameplay.h` + переключение `CreateTestPlayer`
   (п.5).
6. ✅ flecs-зависимость в `components/CMakeLists.txt` +
   `input_event_emitter.h` (п.6).
7. ✅ Рефактор `input_publisher.cpp` на общий `EmitInputEvent` (п.6) —
   компилируется, `z13_module`/`z13_tests` собираются чисто; полную линковку
   `raylib_module` проверить не удалось (см. п.10.2, не связано с этой веткой).
8. ✅ `z13_test_world.h` (п.4).
9. ✅ `input_pipeline_test.cpp` со сценариями из п.8 — код написан,
   компилируется, но **падает в рантайме** — см. п.10.
10. ⛔ Прогнать `ctest --test-dir build` — блокировано, см. п.10.

## 10. Блокер: `InputPipelineTest.*` падает в рантайме сегфолтом

Код тестов (п.8) компилируется и линкуется без ошибок, но при запуске
`z13_test_runner --gtest_filter="InputPipelineTest.*"` падает сегфолтом
внутри `Z13TestWorld`'s `Core::CreateWorld()` → `Z13ModuleFactory::RegisterModules()`
→ `world.import<Z13Module>()`. Это **не регрессия от кода тестов** — структурная
проблема сборки, которую эта ветка первой обнажила (до неё ничто в проекте не
линковало `z13_module` напрямую в один бинарник и не вызывало его C++ API
в процессе — прод грузит `z13_module.so` только через `boost::dll` в рантайме,
`zodiac13` исполняемый файл его вообще не линкует на этапе сборки).

### 10.1 Диагностика (gdb + nm)

Бэктрейс (gdb, `bt`):
```
#0  0x0000000000000000 in ?? ()
#1  ecs_cpp_get_symbol_name (symbol_name=0x0, type_name=... "z13::Z13Module", len=14)
    at .../flecs/src/addons/flecs_cpp.c:107
#2  flecs::_::import<z13::Z13Module> (world=...)
#3  flecs::world::import<z13::Z13Module> (this=...)
#4  z13::Z13ModuleFactory::RegisterModules (this=..., world=...)
    at z13_module_factory.cpp:32
#5  z13::Core::CreateWorld (this=...) at core.cpp:76
#6  z13::testing::Z13TestWorld::CreateWorld (core=...) at z13_test_world.h:61
#7  z13::testing::Z13TestWorld::Z13TestWorld (this=...) at z13_test_world.h:39
```
Исполнение прыгает на адрес `0x0` — вызов через нулевой указатель на функцию,
не просто плохой указатель на данные.

Корень, подтверждённый через `nm`:
```
z13_module.so:  d ecs_os_api             (локальный символ, не в -D/динамической таблице)
z13_module.so:  b ecs_os_api_initialized (тоже локальный)
```
`flecs` подключён как **статическая** библиотека (`flecs::flecs_static`,
`PUBLIC` в `zodiac13::core`, сама `core` — статическая). Каждый `.so`, который
линкует `core` (`z13_module.so`, тестовый `z13_tests.so` и т.д.), получает
**свою независимую копию** внутреннего состояния flecs — включая таблицу
`ecs_os_api` (указатели на OS-функции: malloc/strncpy/и т.д.), которая
инициализируется лениво при первом использовании flecs **в этой конкретной
копии кода**.

`Z13TestWorld` создаёт `flecs::world` внутри `z13_tests.so` — там `ecs_os_api`
инициализируется нормально. Но `Z13ModuleFactory::RegisterModules` — код,
физически лежащий в `z13_module.so`, — вызывает `world.import<Z13Module>()`
**через свою собственную, никогда не инициализированную копию** `ecs_os_api`.
`ecs_cpp_get_symbol_name` внутри дёргает такой нулевой указатель — крэш.

### 10.2 Попытка фикса — не сработала, отменена

Гипотеза: если `core` сделать **SHARED**, а `flecs::flecs_static` останется
`PUBLIC` — CMake всё равно транзитивно заставляет **каждого потребителя**
`core` повторно линковать статический архив flecs поверх динамической связи с
`core.so` (проверено через `nm`: адрес `ecs_os_api` в `z13_module.so` и в
`core.so` различался даже после перевода `core` в SHARED).

Исправил это разделением: `core` линкует `flecs::flecs_static` `PRIVATE`
(архив зашивается ровно один раз, только в `core.so`), а потребителям
пробрасываются только заголовки через отдельный INTERFACE-таргет
`flecs_headers` (только `INTERFACE_INCLUDE_DIRECTORIES`/`_COMPILE_DEFINITIONS`,
без самого архива).

Это тоже не сработало — уже по другой причине: пакет `flecs`, собранный
vcpkg, компилирует свои внутренние C-функции/данные (`ecs_os_api`,
`ecs_cpp_get_symbol_name` и т.д.) со **скрытой (hidden) видимостью**. Эта
видимость — атрибут, зашитый в сами `.o`-файлы архива на этапе сборки
vcpkg-пакетом, и переживает повторную линковку в другую библиотеку. Даже
когда `core.so` — единственное место, куда flecs зашит статически, эти
символы **не попадают в динамическую таблицу экспорта `core.so`** — линковка
`z13_module.so`/`z13_tests.so` против `core.so` падает с `undefined reference
to 'ecs_cpp_get_symbol_name'` и т.д. (проверено `nm -D`: нужных символов нет
ни в одном `.so` в принципе).

**Правки `lib_core/CMakeLists.txt` и `components/CMakeLists.txt` из этой
попытки отменены** (see git history) — оставлять проект в состоянии, где
`raylib_module`/`z13_launcher`/основной `zodiac13` не собираются, ради
эксперимента с непонятным исходом не стал. Полный обход этого блокера
потребовал бы либо пересборки/патча самого vcpkg-порта `flecs` (флаги
видимости), либо другой, более инвазивной схемы линковки — обе опции
выходят за рамки задачи с тестами и требуют отдельного решения.

### 10.3 Проверено, что не затронуто

После отката: `z13_module`, `z13_tests`/`z13_test_runner` собираются чисто;
все 20 ранее существовавших тестов (`CameraLook.*`, `FlecsStateRoundTrip.*`,
`FlecsStateSnapshot.*`) проходят без изменений. `raylib_module` не линкуется
из-за **отдельной, ранее существовавшей** проблемы — конфликт символов
`stb_image` между `libraylib.a` и `assimp_loader.cpp` (не в этой ветке,
подтверждено `git diff` — файл не тронут, последний коммит `5a88fee`).

### 10.4 Открытые вопросы / варианты продолжения

- Бьёт ли эта же проблема по продовому `boost::dll`-пути загрузки
  `z13_module.so`? По имеющимся данным должна бы (hidden-видимость не зависит
  от способа загрузки), но это отдельная гипотеза, требующая отдельной
  проверки на реальном запуске игры — не проверялось в рамках этой ветки.
- Варианты для `InputPipelineTest.*`: пометить `DISABLED_` до решения
  блокера; либо чинить сам vcpkg-порт flecs (сборка с
  `-DCMAKE_C_VISIBILITY_PRESET=default`/аналог); либо искать способ запускать
  этот конкретный тест как отдельный процесс/exe, слинкованный иначе.
