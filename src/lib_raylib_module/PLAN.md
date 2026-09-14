# План: миграция графического модуля Ogre → raylib

Цель: воспроизвести то, что сейчас делает `lib_ogre_module` — окно, камера,
**скайбокс** и **модель космического корабля** — средствами raylib. FBX грузим
через assimp. Ogre и raylib **одновременно не загружаются** (переключение — своп в
`config/z13_config.yaml`).

## 1. Что делает Ogre-модуль сейчас

| Ответственность | Реализация | raylib-эквивалент |
|---|---|---|
| Окно + GL-контекст | SDL2 создаёт окно, Ogre рендерит по хэндлу | `InitWindow()` — raylib владеет окном и GL (glfw3); SDL2 не нужен |
| Цикл кадра | фаза `ReadEvents` → `ReadSdlEvents`; фаза `FinalizeRender` → `renderOneFrame` | те же фазы: опрос ввода raylib; `BeginDrawing`/`EndDrawing` в `FinalizeRender` |
| Сцена | PCZSceneManager, дерево нод, камера, свет | графа сцены нет: `Camera3D` + иммедиатный `BeginMode3D; DrawModel; EndMode3D` |
| Скайбокс | `setSkyBox("skybox/skybox_2")` + `.material` c cubic-текстурой | `GenMeshCube` + шейдер `skybox.vs/fs` (GLSL 330 из примеров raylib) + cubemap из 6 PNG `vz_sunshine_*` |
| Модель корабля | `createEntity("models/spaceship2/spaceship.fbx")` — плагин Ogre Assimp | assimp `aiScene` → raylib `Mesh`/`Model` вручную |
| Синк трансформа | observer `SceneNodeComponent`+`Eigen::Matrix4f` OnSet | observer → `Camera3D` / `model.transform` |
| Камера | Ogre `Camera` на `gameplay::Camera` OnAdd, Z-up, forward `+X` | `Camera3D{position,target,up={0,0,1},fovy,projection}` |
| GUI | Ogre ImGuiOverlay + стек модальных окон | **raygui** (иммедиатный, встроен в raylib), переписать окна — фаза 2 |
| Ввод | SDL → `OgreBites::Event` → `InputPublisher` эмитит flecs-события | новый publisher: `IsKeyPressed/Released`, `GetMouseDelta` → те же flecs-события |
| Строительство | `CreateCubeMesh` на `building::Brush` | `GenMeshCube` → `Model` — фаза 2 |
| Свет | ambient white + spotlight | MVP без света; `rlights.h` — фаза 2 |

`z13_module` (геймплей, ввод→действия, движение камеры через `Eigen::Matrix4f`)
**не меняется** — модуль отдаёт те же компоненты и события.

## 2. Подготовка

1. **vcpkg.json**: `raylib` c `"default-features": false` (без `audio`/miniaudio),
   `raygui`. Пин `raylib` `5.5#1` (**не 6.0** — см. M1-грабли). `assimp`, `imgui`
   уже есть; `imgui` уходит на M4 вместе с Ogre-модулем.
2. Новый `src/lib_raylib_module/` рядом с `lib_ogre_module`. Паттерн модуля тот же:
   SHARED-DLL, `BOOST_DLL_ALIAS(create_module_factory)`, вывод в
   `bin/modules/raylib/`, `add_subdirectory` в корневом `CMakeLists.txt`.
   Дефиниции путей — через `target_compile_definitions` (не `add_definitions`).
3. `config/z13_config.yaml`: `modules/ogre/ogre_module` → `modules/raylib/raylib_module`.
   `lib_ogre_module` остаётся в дереве (не в конфиге) до M4.

## 3. Раскладка модуля (зеркалит Ogre-модуль)

```
src/lib_raylib_module/
  CMakeLists.txt
  PLAN.md                            ← этот файл
  include/raylib_module/
    raylib_module_factory.h          # BOOST_DLL_ALIAS
    raylib_components.h              # RaylibData (Singleton), RenderModel, Skybox, фазы-теги
  src/
    raylib_module_factory.cpp
    raylib_module.{h,cpp}
    raylib_system.{h,cpp}            # InitWindow, ReadInput, Render, камера, shutdown
    render/environment_render_system.{h,cpp}   # рисует скайбокс + модели
    tools/
      window_tools.{h,cpp}          # InitWindow/CloseWindow, DisableCursor, ресайз
      input_publisher.{h,cpp}       # raylib-ввод → z13 flecs-события + карта KeyboardKey → z13::fbs::input::Keycode
      assimp_loader.{h,cpp}         # aiScene → raylib Model (+ резолв путей текстур)
      skybox.{h,cpp}               # 6 PNG → cubemap + шейдер
      math_convert.{h,cpp}         # Eigen::Matrix4f ↔ raylib Matrix, → Camera3D
    shaders/skybox.vs, skybox.fs
  # фаза 2:
    gui/                            # raygui-окна (переписать с imgui)
    building/                       # кубы строительства
```

## 4. Загрузка FBX (`assimp_loader`)

Меши в проекте маленькие — без split/flat/патча raylib.

- Импорт: `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals | aiProcess_FlipUVs`.
  **Без** `aiProcess_PreTransformVertices` — меши не сливаются; трансформы нод
  запекаем сами при обходе `aiNode` (накопление матриц → трансформ позиций/нормалей).
- Каждый `aiMesh` → отдельный raylib `Mesh` в одном `Model` (`meshCount`, `meshMaterial[]`).
- Индексы: `aiFace.mIndices` (`unsigned int`) → сузить в `Mesh.indices` (`unsigned short`):
  ```cpp
  for (face : mesh->mFaces)          // после Triangulate mNumIndices == 3
    for (k = 0..2)
      indices[i++] = static_cast<unsigned short>(face.mIndices[k]);
  ```
  Debug-гард: `assert(mesh->mNumVertices <= 0xFFFF)` + `LOG_WARN`.
- Материал: MVP — только `MATERIAL_MAP_DIFFUSE` из base-color текстуры. PBR
  (normal/metallic/roughness/emissive) — фаза 2.
- Резолв путей текстур (см. §8): `aiMaterial::GetTexture` → basename → искать в
  `<model_dir>/` и `<model_dir_stem>.fbm/`; `*N` → встроенная (`aiScene.mTextures`).

## 5. Скайбокс (`skybox`)

- 6 PNG `assets/textures/skybox/vz_sunshine/vz_sunshine_{rt,lf,up,dn,fr,bk}.png` →
  собрать один `Image` в layout `CUBEMAP_LAYOUT_LINE_HORIZONTAL` (`ImageDraw` по
  6 плиткам) → `LoadTextureCubemap(atlas, layout)`.
- Порядок граней raylib: +X,-X,+Y,-Y,+Z,-Z. Маппинг rt/lf/up/dn/fr/bk и
  ориентацию под Z-up (Ogre крутил скайбокс на `90° X`) подбираем эмпирически.
- `Model skybox = LoadModelFromMesh(GenMeshCube(1,1,1))`; шейдер `skybox.vs/fs`;
  `maps[MATERIAL_MAP_CUBEMAP].texture = cubemap`. Рисуем первым, с
  `rlDisableBackfaceCulling()` + `rlDisableDepthMask()`.
- `.dds`-кубмапы в ассетах не используем (raylib читает DDS-кубмапы ненадёжно).

## 6. Вехи

- **M0 — каркас. ✅** Модуль собирается (SHARED-DLL), грузится по конфигу; `InitWindow`
  640×480 «Zodiac 13», чёрный экран (`BeginDrawing`/`ClearBackground`/`EndDrawing`),
  raylib-логи → spdlog (`SetTraceLogCallback`), `SetExitKey(KEY_NULL)`,
  `SetTargetFPS(0)`. Фазы `ReadEvents`/`PreRender`/`Render`/`PostRender`/`FinalizeRender`
  как в Ogre. `WindowShouldClose` → `RaylibWindowClosed` → observer → `Core::Shutdown()`.
  - **Грабли:** `raylib.h` + `<windows.h>` конфликтуют по `CloseWindow`/`ShowCursor`/
    `DrawText`/`Rectangle` — фикс: `target_compile_definitions(... WIN32_LEAN_AND_MEAN
    NOMINMAX NOGDI NOUSER)`.
  - **DLL-деплой:** vcpkg applocal сам кладёт `raylib.dll`/`glfw3.dll`/… рядом с
    `raylib_module.dll`; exe грузит модуль через `LOAD_WITH_ALTERED_SEARCH_PATH`,
    вся цепочка резолвится из `bin/modules/raylib/`. Свой `$<TARGET_RUNTIME_DLLS>`-копир
    не нужен (и он не видит `glfw3.dll`). `FLAG_WINDOW_HIDDEN` — на M1 (когда появится загрузка).
- **M1 — камера + скайбокс. ✅**
  `RaylibCamera` из `gameplay::Camera` (fov→fovy, `CAMERA_PERSPECTIVE`, up `{0,0,1}`),
  поза из `Eigen::Matrix4f` (`OnAddCamera` — начальная, observer `Eigen::Matrix4f`
  OnSet — покадрово; `math_convert.UpdateCameraFromTransform`: forward = R.col(0),
  up = R.col(2)). Скайбокс: 6 PNG `vz_sunshine_*` → атлас `LINE_HORIZONTAL` →
  `LoadTextureCubemap` → шейдер `assets/shaders/skybox.{vs,fs}` (GLSL 330) →
  `GenMeshCube`. Кадр разложен по фазам: `FrameBegin` (`PreRender`:
  `BeginDrawing`+`ClearBackground`), `EnvironmentRenderSystem::Draw` (`Render`:
  `BeginMode3D` → `DrawSkybox` → `EndMode3D`), `FrameEnd` (`FinalizeRender`:
  `EndDrawing`+`WindowShouldClose`).
  - Загрузка скайбокса — observer на `RaylibData` OnAdd (не `InitWorldDataEvent`):
    гарантированно **после** `InitWindow` (иначе `LoadTextureCubemap` до GL-контекста
    → `WARNING: GPU is not ready`).
  - Ориентация: `skybox.vs` учитывает `matModel`; `DrawSkybox` крутит куб на
    `+90° X` (Y-up грани `vz_sunshine` → Z-up мир, как quaternion в Ogre `setSkyBox`).
    Проверено облётом камеры по yaw — грани стыкуются, «верх» сверху.
  - Shutdown: `RaylibSystem::Shutdown` снимает `Skybox` до `CloseWindow`
    (GPU-teardown при живом контексте). `SkyboxResources` — `shared_ptr` c dtor
    (D9), обнуляет id шейдера/кубмапы в `model.materials[0]` перед `UnloadModel`.
  - **Грабли: raylib 6.0 из vcpkg виснет.** Пустое окно «Не отвечает»,
    `wglSwapBuffers` не презентит (>4000 fps при vsync, `GetFPS()==0`), при этом
    `glReadPixels`/`TakeScreenshot` корректны и flecs-цикл крутится. Причина —
    инжектор Nahimic / A-Volute (`AudioDevProps2.dll` из `C:\ProgramData\A-Volute\`).
    Воспроизводится на голом raylib 6.0, на iGPU и dGPU. raylib **5.5** с тем же
    инжектором работает. Ogre-модуль не задет — у него своё Win32/WGL окно.
    **Не помогло** (не повторять): SDL2-бэкенд (голый тест ок, движок так же виснет),
    `FreeLibrary` инжекта (держит хук `SetWindowsHookEx`), unhook `SwapBuffers`
    (хука нет — пролог и IAT чистые), `DisableProcessWindowsGhosting` (убирает
    «Не отвечает», окно всё равно чёрное), kick ресайзом/minimize. Решение — пин
    `5.5#1`. API-различий 5.5↔6.0 для нашего кода нет.
    Логи чистые: `window created` → `skybox loaded (512x512 per face)` → `RaylibCamera created`.
- **M2 — модель корабля. ✅** `tools/assimp_loader` (`LoadModelFromAsset`):
  `Triangulate | JoinIdenticalVertices | GenNormals | FlipUVs`, обход `aiNode` c
  запеканием трансформов нод в вершины, `aiMesh` → raylib `Mesh` (позиции/нормали/
  UV0), индексы `uint`→`ushort` c Debug-гардом (`> 65535` → `LOG_WARN` + `assert`).
  Материал: diffuse/base-color текстура. Резолв: `aiScene::GetEmbeddedTexture(ref)`
  (ловит и `*N`, и FBX-media по имени файла) → `model_dir/ref` → `model_dir/base`
  → `<stem>.fbm/base` → сам путь.
  `RenderModel` (Singleton, `shared_ptr<ModelResources>` c dtor `UnloadModel`)
  грузится в том же observer, что и скайбокс; рисуется в фазе `Render` после него,
  TRS запечён в `model.transform` (`EigenToRaylibMatrix` из `Affine3f`: T·R·S,
  R = `Rx(90°)·Ry(−90°)`, как в Ogre `LoadDemoMesh`).
  - **JPEG в raylib выключен** (vcpkg-сборка без `SUPPORT_FILEFORMAT_JPG`). В
    `assimp_loader` вкомпилен `stb_image` (`STBI_ONLY_JPEG/PNG`, `STB_IMAGE_IMPLEMENTATION`),
    `DecodeImage` пробует raylib, для `.jpg/.jpeg` — сразу `stbi_load_from_memory`.
  - `spaceship2/spaceship.fbx` **встраивает** base-color JPEG (5 `Video`-нод в FBX),
    текстуры на диске нет. Грузится через `GetEmbeddedTexture` + stb. Лог:
    `assimp: '…' -> 1 mesh(es), 23457 verts, 1 material(s) (1 textured)`.
    Normal/metallic/roughness/emissive — тоже встроены, но это PBR-карты, M3+.
  - Winding/culling — силуэт цельный, backface culling по умолчанию ОК (§8.7 снят).
- **M3 — паритет (фаза 2).** Разбит:
  - **3.1 Ввод ✅** `tools/input_publisher` — система фазы `ReadEvents` c `.immediate()`
    (обязательно: `WorldNoDeferGuard` + синхронный `.emit()` легален только из
    недефернутой системы, как Ogre `ReadEventsSystem`). Table-driven `KeyboardKey`→
    `z13::fbs::input::Keycode` (только биндинги из `actions.fbs`), мышь → те же
    `z13::input::*Event`. Компонент-снапшот `RaylibInputFrame` (позиция/дельта/колесо).
    Проверено: WASD летит камерой, мышь крутит вид, Esc → пауза.
  - **3.2 Relative-mouse ✅** `DisableCursor`/`EnableCursor` по `world.has<gameplay::Pause>()`
    прямо в системе (не observer — `.each` на пустом теге `Pause` роняет MSVC 14.51 ICE).
  - **3.3 Свет ✅** `render/lights` (мини-`rlights`, Blinn-Phong), `assets/shaders/lighting.{vs,fs}`,
    один направленный «sun», шейдер шарится на материалы корабля (`ModelResources`
    обнуляет borrowed shader id перед `UnloadModel`). `viewPos` пушится каждый кадр.
  - **3.4 Строительство ✅** `BuildingBlock` (per-entity, `shared_ptr<ModelResources>`),
    observer на `Brush`+`Eigen::Matrix4f` OnAdd/OnSet/OnRemove → `GenMeshCube(0.6)`,
    рисуется через кэш-query внутри Render-паса. TAB включает/выключает превью-куб.
  - **3.5 Resize ✅** Ничего писать не пришлось: raylib пересчитывает проекцию из
    `GetScreenWidth/Height` каждый кадр, вьюпорт следует за render size. Проверено
    640→1000×600→500×700 — без искажений и чёрных полей, `fovy` фикс, горизонт. FOV
    адаптируется.
  - **3.6 raygui-GUI ✅.** `raygui` `4.0` из vcpkg (header-only,
    `RAYGUI_IMPLEMENTATION` в `gui/gui_system.cpp`, компилится с `NOGDI NOUSER`).
    Полный стек окон, порт с imgui:
    - `gui/gui_windows.{h,cpp}` — база `Window` (панель + `DrawBody` + `Draw()`
      возвращает `StackRequest`) + `WindowStack` (singleton, `vector<shared_ptr>`).
      Окна: **MainMenu** (Resume→CloseMenu / Settings… / Exit→`RaylibWindowClosed`),
      **InputSettings** (Invert X/Y, Sensitivity-слайдер прямо в `InputConfig`,
      Save-on-dirty → `SaveConfigEvent`), **KeyBindings**.
    - `gui/gui_keybindings.{h,cpp}` — `BuildKeyBindingModel` (обход `ActionMap` +
      `InputConfig.keycode_binding`), keycode→текст через flatbuffers-reflection
      (`InputConfigBinarySchema`), `RebindSlot` (уникальность (group,keycode)),
      `ApplyKeyBindingModel` (перезапись `keycode_binding` + `SaveConfigEvent` +
      `OnConfigUpdatedEvent`). 2 слота на действие, «…» = не назначено.
    - `gui_system.cpp`: система `PostRender` рисует `stack.back()->Draw()` и
      применяет `StackRequest`; меню открывается/закрывается по `world.has<Pause>()`
      (не observer — `.each` на пустом теге `Pause` роняет MSVC 14.51 ICE).
      Observer-ы `<WindowBackEvent, WindowStack>` / `<WindowKeyDownEvent, WindowStack>`
      (пустое событие + singleton-компонент — так же обходит ICE) → `OnBack()`/
      `OnKeyDown()` верхнего окна.
    Проверено: Esc → Main → Settings → Keyboard bindings, Esc откатывает по уровню,
    Esc на Main → продолжение; рекординг клавиши (клик слота → «Press a key…» →
    нажать клавишу → бинд, появляется «Save changes»).
    - **Фиксы после первого прохода:** (1) `input_publisher.kKeyMap` расширен до
      ~80 клавиш (A-Z/0-9/F1-F12/стрелки/nav/модификаторы/пунктуация) — иначе
      рекординг не видел нажатие. (2) Панель KeyBindings считает высоту от числа
      групп/действий (была фикс. 420×340, обрезала группу Building и Save).
      (3) `OnKeyDown` игнорит мышиные коды и кадр самого клика (`arm_countdown_`),
      иначе `WindowKeyDownEvent{MOUSE_BUTTON_LEFT}` от клика мгновенно биндил слот
      в «MLeft» (баг, помеченный ещё в Ogre-коде).
- **Фаза D — архитектура «SDL владеет окном, raylib = rlgl-модуль, ImGui вместо raygui».**
  По `plan-migracii-ogre-raylib-sdl-imgui.md`. Валидировано на стенде
  `experiments/sdl_raylib_cube/`. Что сделано в модуле:
  - **`src/platform/sdl_platform.{h,cpp}`** — SDL3 владеет окном / GL-контекстом /
    циклом событий. `Init`: `SDL_HINT_NO_SIGNAL_HANDLERS`, атрибуты GL 3.3 core
    пиннятся явно, `SDL_CreateWindow(OPENGL|RESIZABLE)`, `SDL_GL_CreateContext`,
    `rlLoadExtensions(SDL_GL_GetProcAddress)` → `rlglInit`. `PumpEvents` буферит
    `SDL_Event` за кадр (их разбирают и input, и GUI). `EndFrame`:
    `rlDrawRenderBatchActive` + `ResetGLStateToBaseline` (rlgl-обёртки, plan §4) +
    `SDL_GL_SwapWindow`. `ForwardRaylibLog` переехал сюда из `window_tools`.
  - **raylib — только rlgl-standalone.** Нет `InitWindow`/`BeginDrawing`/`IsKeyDown`/
    `GetFrameTime`/`GetScreenWidth`. `BeginMode3D`/`EndMode3D` → `BeginScene3D`/
    `EndScene3D` в `environment_render_system` (`MatrixPerspective` из `WindowSize`
    синглтона + `rlSetMatrixProjection/Modelview`). `window_tools.{h,cpp}` удалён.
  - **`src/gui/` — raygui → Dear ImGui** (`imgui[sdl3-binding,opengl3-binding]` из
    vcpkg). `gui_system.cpp`: init ImGui по observer-у на `RaylibData` OnAdd (тот же
    триггер, что грузит GPU-ресурсы; порядок InitWorldData-observer-ов не гарантирован).
    Фаза `ReadEvents` → `ImGui_ImplSDL3_ProcessEvent` по буферу + `NewFrame`; фаза
    `PostRender` → стек окон + `ImGui::Render` + `RenderDrawData`. `ShutdownImGui`
    зовётся из `RaylibSystem::Shutdown` до `SdlPlatform::Shutdown` (нужен живой GL).
    `gui_windows.{h,cpp}`: база `Window` теперь `ImGui::Begin` (авторазмер, центр по
    `GetMainViewport()->GetCenter()`), `DrawBody()` без аргумента; виджеты —
    `ImGui::Button/Checkbox/SliderFloat/Text`. `RowLayout`/`GetScreenWidth`/
    `DrawRectangle`/`GuiPanel` убраны. `gui_keybindings.{h,cpp}` — без изменений.
  - **`CMakeLists.txt`**: `find_package(SDL3)` + `find_package(imgui)`, линк
    `SDL3::SDL3` + `imgui::imgui`; `PRIVATE` include-dir `src/` (кросс-каталожные
    `platform/…` / `render/…` / `gui/…`).
  - Проверено на raylib 5.5: окно рисует сцену (скайбокс + корабль + свет),
    Responding=true, зависания нет; Esc → ImGui-меню поверх 3D, Esc → продолжение.
  - **D7 — raylib 6.0 ✅.** `vcpkg.json` override `5.5#1` → `6.0`. Собирается без
    правок (наше использование — только rlgl standalone). Окно рисует сцену,
    Responding=true, зависания **нет** — раз SDL владеет окном, путь GLFW-зависания
    raylib 6.0 (M1-грабли) обойдён. Пин 5.5 снят.
  - **Грабли:** Nahimic3 (запущен на дев-ноуте) шлёт `SDL_EVENT_WINDOW_CLOSE_REQUESTED`
    + `SDL_EVENT_QUIT` окну SDL+GL примерно через 15-25 c — окно закрывается само.
    Бьёт и по стендам `sdl_raylib_cube*` (их валидировали до старта Nahimic3), и на
    5.5, и на 6.0. Не наш код, среда. Обходы: закрыть Nahimic3 / `BlackApps.dat`.
- **M4 — удаление Ogre. ✅** `src/lib_ogre_module` удалён из дерева целиком (был уже
  отключён от сборки на M0); `add_subdirectory` и комментарий про него убраны из
  корневого `CMakeLists.txt`, закомментированная строка `modules/ogre/ogre_module`
  убрана из `config/z13_config.yaml`. `vcpkg.json` правок не потребовал — `ogre`/
  `sdl2`/`pugixml`/`zip`/`poly2tri` были выведены из манифеста ещё на M0, когда
  сборка Ogre-модуля отключалась (`imgui` остался — теперь часть raylib-модуля).
- **M5 (позже) — скелетная анимация FBX.** raylib не грузит анимацию из FBX ни в
  одной версии (только IQM/glTF/M3D) → кости + кейфреймы тянем из assimp сами
  (`Mesh.boneIds`/`boneWeights`, `boneMatrices` по кадрам, шейдер скиннинга).
  raylib 5.5 уже даёт GPU skinning + CPU-анимацию; блендинг клипов из 6.0 —
  хэндроллить (линейная интерп. позы по весу перехода). Не требует 6.0.

## 7. Решения

| # | Вопрос | Решение |
|---|---|---|
| D1 | GUI | Фаза 2, на raygui (встроенный в raylib); MVP без GUI |
| D2 | Математика | `Eigen` для компонента `Eigen::Matrix4f`; конверт в raylib `Matrix` на границе рендера (оба column-major → часто прямой `memcpy`) |
| D3 | Свет | MVP без света; `rlights.h` — фаза 2 |
| D4 | Координаты | Z-up проекта; `Camera3D.up = {0,0,1}` |
| D5 | Материалы | MVP — base color; PBR — фаза 2 |
| D6 | `lib_ogre_module` | Удалён из дерева на M4 |
| D7 | FBX-индексы | Сужать `unsigned int` → `unsigned short` (меши маленькие), Debug-гард |
| D8 | Частота кадров | Владеет `Core`. raylib: `SetTargetFPS(0)`, `FLAG_VSYNC_HINT` off — внешний драйв поддерживается (§8.1) |
| D11 | Версия raylib | `6.0` (Фаза D7). Пин `5.5` был из-за зависания raylib-6.0-GLFW от инжектора Nahimic (M1-грабли) — снят, т.к. в архитектуре «SDL владеет окном» пути GLFW нет |
| D9 | Время жизни GPU-ресурсов | `std::shared_ptr<Model/Texture/Shader>` с кастомным делитером (`UnloadX`). Позже можно на move-only + flecs hooks |
| D10 | Карта ввода | Не заполнять целиком: table-driven маппинг для нужных клавиш + компонент-снапшот сырого ввода кадра для «добора» остального (§8.15) |

## 8. Потенциальные проблемы (обсуждается)

Список ниже — то, что нужно проговорить до старта. Уточняется по ходу.

1. **Частота кадров — у `Core` (решено, D8).** raylib поддерживает внешний драйв:
   `Begin/EndDrawing` — обычные функции, свой `while` не нужен. `SetTargetFPS(0)`
   убирает внутреннее ожидание, `FLAG_VSYNC_HINT` off — vsync. `EndDrawing()` тогда
   делает только swap + `PollInputEvents()` (при желании их можно звать раздельно:
   `SwapScreenBuffer()` / `PollInputEvents()` / `WaitTime()` доступны по отдельности).
   `delta` — из `world.delta_time()`. `GetFrameTime()`/`GetFPS()` в raylib станут
   недостоверны — мы ими не пользуемся (камера гонится из `Eigen`).
2. **`PollInputEvents()` один раз за кадр.** raylib опрашивает ввод внутри
   `EndDrawing()`. Наша фаза `ReadEvents` в начале `world.progress()` читает
   состояние с прошлого `EndDrawing` — порядок правильный (латентность ~0), но
   `PollInputEvents` нельзя звать дважды.
3. **Пути ассетов.** Резолвить от каталога exe (`GetApplicationDirectory()` /
   `boost::dll::program_location`), а не от CWD. Post-build копирует `assets/` и
   `shaders/` в `bin/`.
4. **Текстуры внутри FBX.** Пути могут быть абсолютными с машины художника,
   относительными, или встроенными (`*N`). Нужен резолвер: basename → `<model_dir>`,
   `<stem>.fbm/`; `*N` → `aiScene.mTextures` (может быть сжатый PNG-блоб).
5. **Оси и единицы модели.** FBX обычно Y-up, см. Ogre Assimp-кодек конвертил сам.
   Нам — запекать корневой поворот Y-up→Z-up (`90° X`) или ставить assimp-конфиг.
   Ожидается подбор ориентации на M2.
6. **Скайбокс: порядок граней + поворот.** 6 PNG → атлас, порядок +X…−Z, плюс
   Z-up-поворот. Практически всегда с первого раза неправильно — итерируем.
7. **Хендедность / отсечение.** Z-up + своя матрица камеры: при ошибке виндинга
   модели выворачиваются наизнанку (backface culling). Готовиться крутить
   `rlSetCullFace` / winding.
8. **raygui — это переписывание, не порт.** Иммедиатный режим, ретейн-состояния
   нет: каждое окно (`Window`-объект) хранит своё состояние виджетов (скроллы,
   текст-буферы, `editMode`), модальность через `GuiLock/GuiUnlock`. Захват
   клавиши для ребайндинга — `GetKeyPressed()`. Объём — фаза 2.
9. **Порядок init синглтона.** `RaylibData` (Singleton) должен существовать, когда
   `z13_module` добавляет `gameplay::Camera`. Ogre решает это цепочкой observer-ов
   `RegisterComponentsEvent`/`InitSystemsEvent`/`InitWorldDataEvent` + `yield_existing`
   + singleton-семантикой flecs. Повторить один-в-один.
10. **Время жизни GL-контекста при shutdown.** `UnloadModel/Texture/Shader` — до
    `CloseWindow` (нужен живой контекст). Через observer-ы OnRemove, как в Ogre.
11. **raylib — глобальный синглтон.** Одно окно на процесс. `CreateWorld()`
    больше одного раза → второй `InitWindow` упадёт. Сейчас мир один — фиксируем
    как ограничение.
12. **Рантайм-DLL.** Копировать `$<TARGET_RUNTIME_DLLS>` (raylib.dll, glfw3.dll,
    assimp.dll + его зависимости) и рядом с модулем, и рядом с `zodiac13.exe`
    (модуль грузится exe через `LOAD_WITH_ALTERED_SEARCH_PATH`). raygui — хедер,
    dll нет. `RAYGUI_IMPLEMENTATION` — ровно в одном `.cpp`.
13. **CI.** PR-workflow собирает только `z13_test_runner` — падение сборки
    raylib-модуля не поймает. Рендер-тесты в headless CI невозможны (нет GPU).
    Максимум — добавить сборку модуля/`zodiac13` в CI как build-only проверку.
14. **assimp/FBX в vcpkg.** Убедиться, что порт собран с FBX-импортёром (обычно
    да). Бинарный и ASCII FBX — assimp тянет оба.
15. **Карта ввода — упрощённая + механизм добора (решено, D10).**
    `input_publisher` под SDL мапит `SDL_KeyCode` → `z13::fbs::input::Keycode`
    (~130 записей). Для raylib делаем table-driven `KeyboardKey` → `z13::fbs::input::Keycode`
    только для реально нужных клавиш (движение, escape, модификаторы). Плюс —
    компонент-снапшот `RaylibInputFrame` (сырое состояние кадра: mouse pos/delta/
    wheel, `IsKeyDown` по запрошенным кодам, `GetCharPressed`-очередь), чтобы любая
    система могла достать больше, чем публикуется событиями. Отсутствуют против SDL:
    `clicks` (двойной клик) — проверить потребителей в `gameplay_input_system`/GUI,
    если не нужны — ставить `1`; key-repeat — `IsKeyPressedRepeat` (raylib 5.0+).
16. **Версии raygui ↔ raylib.** `raygui` использует внутренности raylib — пинить
    обе в `vcpkg.json`, проверить что порт `raygui` поддерживает `raylib 6.0`
    (raygui обычно отстаёт от raylib).
17. **Логи raylib.** `TraceLog` идёт в stdout. `SetTraceLogCallback` → форвард в
    spdlog; `SetTraceLogLevel(LOG_WARNING)` чтобы не засорять.
