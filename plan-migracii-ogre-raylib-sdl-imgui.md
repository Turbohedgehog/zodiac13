# План: SDL + raylib (rlgl standalone) + ImGui — миграция с Ogre3D

## 0. Исходные ограничения (зафиксировано в обсуждении)

- **SDL владеет окном, GL-контекстом, event loop, input и таймингом.** Ни raylib, ни ImGui не должны диктовать цикл приложения.
- **raylib используется только как рендер-модуль** через `rlgl` в standalone-режиме (без `InitWindow`/`BeginDrawing`/`WindowShouldClose`). Модуль должен быть отключаем в рантайме.
- **ImGui не должен знать о raylib.** Используются только официальные бэкенды `imgui_impl_sdl2`/`imgui_impl_sdl3` + `imgui_impl_opengl3`.
- Подтверждено: подход рабочий на актуальной **raylib 5.5** (`rlgl_standalone.c` — официальный, поддерживаемый пример в составе репозитория).
- Открытый риск: возможные крэши от сторонних GL-инжектов (Nahimic и подобные) — причина ещё не локализована до конца (профиль контекста одинаковый у вас в обоих случаях, значит дело, скорее всего, в состоянии GL на момент `SwapBuffers`, либо в наличии/отсутствии слоя ImGui). Это отдельный пункт диагностики ниже — закладываем меры профилактики уже на уровне архитектуры.

---

## 1. Архитектура слоёв

```
┌─────────────────────────────────────────────┐
│  Application / Game logic                    │  ← не знает про GL вообще
├─────────────────────────────────────────────┤
│  Platform layer (SDL)                        │  ← окно, input, timing, GL-контекст
├───────────────────┬───────────────┬─────────┤
│  Render module     │  UI layer      │ (другие │
│  (raylib / rlgl)   │  (ImGui)       │ модули) │
│  ОТКЛЮЧАЕМ          │  независим     │         │
└───────────────────┴───────────────┴─────────┘
```

Правило: **render module и UI layer не имеют друг о друге знаний.** Единственная общая точка — активный GL-контекст SDL и дисциплина по GL-состоянию (см. п.4).

### 1.1 Platform layer (владелец: SDL)
- Создание окна, `SDL_GL_CreateContext`
- Event loop, `SDL_PollEvent`
- Ввод (клавиатура/мышь/геймпад) — свой слой абстракции, **не** через raylib `IsKeyDown`/`GetMouseDelta` (они требуют `CORE`, которого нет без `InitWindow`)
- Тайминг (`SDL_GetTicks64` / `SDL_GetPerformanceCounter`) вместо `GetFrameTime()`
- Размер окна — свой источник правды, транслируется в render-модуль и в ImGui вручную (raylib без `InitWindow` не знает `GetScreenWidth/Height`)

### 1.2 Render module (raylib / rlgl standalone)
- Инициализация: `rlLoadExtensions`, `rlglInit(w, h)`, `rlViewport`, проекционная матрица
- Публичный интерфейс модуля — что-то вроде:
  ```cpp
  struct IRenderModule {
      virtual bool Init(int w, int h) = 0;
      virtual void Resize(int w, int h) = 0;
      virtual void RenderFrame(const SceneData&) = 0;
      virtual void Shutdown() = 0;
  };
  ```
- Реализация на raylib — **один из вариантов** `IRenderModule`, полностью заменяемый/отключаемый (в духе того, как раньше был Ogre)
- Внутри — обычный raylib content API (`DrawTexture`, `DrawModel`, `DrawText`...), т.к. он не завязан на `CORE` (см. таблицу из предыдущего обсуждения)

### 1.3 UI layer (ImGui)
- `ImGui_ImplSDL2_InitForOpenGL` / `ImGui_ImplOpenGL3_Init`
- Полностью самодостаточен, получает события от Platform layer, рисует в любом случае — независимо от того, включён render-модуль или нет (полезно для debug/dev-режима, когда рендер выключен, а UI/консоль должны работать)

---

## 2. Порядок инициализации

1. `SDL_Init`, создать окно с `SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE`
2. Запросить атрибуты GL-контекста **до** `SDL_GL_CreateContext`: версия, профиль, double buffer, depth/stencil bits — зафиксировать явно, не полагаться на дефолты (важно для повторяемости и для диагностики оверлей-краша)
3. `SDL_GL_CreateContext` + `SDL_GL_MakeCurrent`
4. Загрузка GL-функций (glad/GLEW — единый загрузчик на весь процесс, не плодить разные загрузчики для rlgl и для остального кода)
5. Инициализация render-модуля (`rlLoadExtensions` → `rlglInit`), если включён
6. Инициализация ImGui-бэкендов
7. Только после этого — вход в главный цикл

---

## 3. Главный цикл (эскиз)

```cpp
bool running = true;
while (running) {
    // 1. Platform: события и таймер
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        platformInput.Process(e);
        if (e.type == SDL_QUIT) running = false;
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED)
            OnResize(e.window.data1, e.window.data2); // -> render module + rlViewport
    }
    float dt = platformClock.Tick();

    // 2. Game logic
    app.Update(dt, platformInput);

    // 3. Render module (опционален)
    if (renderModule && renderModule->IsEnabled()) {
        renderModule->RenderFrame(app.GetSceneData());
        ResetGLStateToBaseline(); // см. п.4 — обязательно перед следующим слоем/swap
    } else {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // если рендер выключен, нужен хоть какой-то фон
    }

    // 4. UI layer
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    app.DrawUI();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // 5. Present
    SDL_GL_SwapWindow(window);
}
```

---

## 4. Гигиена GL-состояния (профилактика крэшей от оверлеев/инжектов)

Это прямое следствие обсуждения проблемы с Nahimic. Независимо от того, подтвердится ли причина, **дисциплина по состоянию перед `SwapBuffers` — это просто хорошая практика**, которая параллельно снижает риск конфликтов со сторонними хуками (Nahimic, RTSS, Discord overlay и т.п. — весь этот класс инжектов чувствителен к нетипичному GL-состоянию на момент презента):

```cpp
void ResetGLStateToBaseline() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glViewport(0, 0, windowW, windowH);
}
```

Вызывать **после** `renderModule->RenderFrame()` и **после** `rlDrawRenderBatchActive()`, перед тем как передать управление ImGui/swap. Это же место — первый кандидат на A/B-тест: если баг с Nahimic воспроизводится, попробовать вызывать `ResetGLStateToBaseline()` и до, и после ImGui-рендера, и смотреть, меняется ли частота крэша.

---

## 5. Диагностика открытого вопроса с Nahimic (до начала полноценной миграции)

Отдельная короткая исследовательская задача, лучше сделать её до того, как логика приложения обрастёт кодом:

1. Собрать минимальный репро: SDL + rlgl (пустая сцена) + ImGui, без остальной логики
2. Проверить, воспроизводится ли крэш на этом минимальном стенде
3. Если да — попеременно убирать слои (сначала ImGui, потом rlgl) и смотреть, при каком минимальном наборе крэш пропадает
4. Сравнить дамп крэша (call stack) — если падает внутри `NahimicOSD.dll`/`AudioDevProps2.dll`, это подтверждает внешнюю причину
5. Занести exe в blacklist Nahimic как fallback-меру для пользователей (`...\A-Volute.Nahimic\Modules\Scheduled\Configurator\BlackApps.dat`), независимо от результата — это стандартная практика в геймдеве

---

## 6. Особенности миграции логики с Ogre3D

Это отдельный по объёму пласт работы, не связанный с рендер-циклом напрямую:

- **Сцен-граф.** У Ogre есть встроенный scene graph (`SceneManager`, `SceneNode`). У raylib его нет — нужен свой (даже плоский список с ручной трансформацией — ок для начала, но заложите интерфейс, чтобы потом не переписывать всё)
- **Камера.** Ogre camera → `Camera3D`/`Camera2D` raylib, либо своя матрица (т.к. вы и так не используете `UpdateCamera()` — она завязана на CORE-инпут)
- **Материалы/шейдеры.** Ogre `.material` скрипты → raylib `Shader`/`Material` — прямого конвертера нет, миграция вручную, разумно делать поэтапно по ассетам, не одним махом
- **Ресурс-менеджмент.** Ogre `ResourceGroupManager` → свой loader поверх `rlLoadTexture`/`rlLoadShaderProgram` (низкоуровневые rlgl-функции, не привязанные к CORE)
- **Рекомендация по стратегии:** мигрировать не "всё сразу", а по одной подсистеме, держа Ogre-рендер и raylib-рендер как два взаимозаменяемых `IRenderModule` одновременно — это прямо соответствует вашему изначальному требованию "рендер — отключаемый модуль", так что архитектура уже даёт готовый путь для parallel run и постепенного переключения

---

## 7. Чек-лист перед стартом кода

- [ ] Зафиксирована версия raylib (например, тег 5.5), обновление — только с ручной сверкой diff'а `rlgl.h`
- [ ] Явно зафиксированы атрибуты GL-контекста (версия, профиль, double buffer, depth/stencil), не дефолты SDL
- [ ] Единый загрузчик GL-функций на весь процесс
- [ ] `IRenderModule` интерфейс — заложен с первого дня, raylib — одна из реализаций
- [ ] `ResetGLStateToBaseline()` встроен в цикл с первого дня, не как заплатка потом
- [ ] Минимальный репро-стенд для диагностики Nahimic собран отдельно от основного проекта
- [ ] Input/timing полностью на стороне SDL, нигде не используются CORE-зависимые функции raylib
