# Запуск zodiac13 в Termux на Android

Сборка (`cmake`/`vcpkg`, см. CLAUDE.md) внутри `proot-distro` на Termux работает как
обычный Linux ARM64 (`arm64-linux` triplet) — специфика начинается на этапе сборки
под нагрузкой и на этапе запуска графики.

## Сборка: ограничивайте параллелизм

PRoot не полностью эмулирует `waitpid()` при большом числе одновременных дочерних
процессов. При сборке с высоким `-j` (например, `-j$(nproc)` на 8-ядерном телефоне)
это может проявиться как:

```
ninja: fatal: waitpid(<pid>): Function not implemented
```

После такой ошибки процесс-обёртка (`cmake --build ... --target install`) не всегда
завершается сам — может зависнуть и продолжать жрать CPU вхолостую, ничего не строя
дальше. Если это случилось во время `vcpkg install` (пересборка зависимости из
исходников), он держит файловую блокировку `vcpkg_installed/vcpkg/vcpkg-running.lock`
и вся последующая сборка будет виснуть в ожидании этой блокировки.

**Рекомендация:** собирать с `-j2` (максимум `-j4`), а не с полным числом ядер:

```sh
cmake --build build -j2
```

**Если сборка «висит и не двигается»:**
1. Проверить, кто держит блокировку: `fuser vcpkg_installed/vcpkg/vcpkg-running.lock`.
2. Проверить, реально ли процесс работает (а не завис): смотреть, растёт ли
   `utime`/`stime` в `/proc/<pid>/stat` за несколько секунд, и есть ли свежие файлы в
   `/opt/vcpkg/buildtrees/<port>/...`; проверить `install-*-err.log` на ту самую
   ошибку `waitpid`.
3. Если подтвердилось, что процесс сломан (ошибка в логе, файлы не обновляются) —
   убить всю цепочку (`vcpkg install` → `cmake` → `ninja`/сборочный процесс) и
   перезапустить сборку с меньшим `-j`. Если процесс, наоборот, ещё активен —
   не трогать, дождаться его окончания.

## Запуск: нужен desktop OpenGL 3.3 Core, а не GLES

Платформенный слой создаёт GL-контекст явно как desktop OpenGL 3.3 Core
(`src/lib_raylib_module/src/platform/sdl_platform.cpp`, `SdlPlatform::Init`):

```cpp
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
```

Драйверы GPU на Android нативно отдают только OpenGL ES, а не desktop GL — поэтому
без прослойки `SDL_GL_CreateContext` просто не создастся, и `SdlPlatform::Init`
вернёт `false`.

**Что нужно для запуска на телефоне:**

1. **X11-дисплей.** Termux сам по себе — терминал без оконного менеджера. Нужно
   отдельное приложение **Termux:X11** (не apt-пакет — отдельный APK), которое
   поднимает X-сервер и обычно само прокидывает `DISPLAY` и доступ к `/dev/dri` в
   proot-сессию через свой стартовый скрипт.
2. **Desktop GL поверх Vulkan (Zink/virgl).** В типовых proot-дистрибутивах под
   Termux:X11 уже стоит Mesa с нужными кусками — `libGLX_mesa.so` и набор
   Vulkan ICD-драйверов под конкретные GPU (например `libvulkan_freedreno.so` для
   Adreno/Turnip, `libvulkan_panfrost.so` для Mali, `libvulkan_lvp.so` —
   программный фолбэк). Наличие этих библиотек ещё не гарантирует работу — нужен
   реально запущенный X-сервер и доступ к `/dev/dri` (без Termux:X11 обращение к
   `/dev/dri` обычно даёт `Permission denied`).
3. **Производительность непредсказуема** и сильно зависит от зрелости
   Vulkan-драйвера под конкретный SoC. Если аппаратный Vulkan-драйвер недоступен,
   Mesa упадёт на программный рендеринг (`llvmpipe`) — для 3D-сцены в реальном
   времени это, скорее всего, будет заметно медленно (единицы FPS).
4. **Ввод.** Текущие биндинги рассчитаны на клавиатуру и мышь
   (`src/lib_raylib_module/src/gui/gui_keybindings.cpp`,
   `src/lib_raylib_module/src/tools/input_publisher.cpp`). SDL3 умеет мапить тач в
   мышь, но для комфортной игры на телефоне понадобится Bluetooth-клавиатура и мышь.

**Проверить перед запуском:**

```sh
echo $DISPLAY                 # непусто, если Termux:X11 запущен и подключён
ls -la /dev/dri                # не должно быть "Permission denied"
ldconfig -p | grep -i "GLX_mesa\|vulkan_freedreno\|vulkan_panfrost\|vulkan_lvp"
```

Если `DISPLAY` пуст или `/dev/dri` недоступен — сначала запустить Termux:X11 на
телефоне (это делается вручную, отдельным Android-приложением, не из этой сессии) и
только после этого запускать бинарник проекта.
