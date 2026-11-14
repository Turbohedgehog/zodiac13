# План: локальная сборка APK и запуск игры на телефоне

## Контекст

APK сейчас собирает только CI (`.github/workflows/android.yml`, `docs/android-plan.md`, «Сделано `f/android-build`»): NDK и `aapt2` от Google под aarch64 Linux нет. Цель — собирать APK на самом телефоне (PRoot Debian под Termux, aarch64, 8 ядер, 11 ГБ ОЗУ, `.claude/skills/build-and-test/references/proot.md`), ставить его и запускать игру, не дожидаясь CI. Нужно для быстрых итераций сенсорного управления, которое проверяется только на устройстве.

Решение о сборке только в CI (2026-10-14) остаётся для PR и релизов: локальная сборка — дополнительный путь, а не замена. Протокол и код игры не меняются; план про инструменты и скрипты.

## Что уже известно

Проверено на машине 2026-11-10:

- В Debian (arm64) есть `openjdk-21-jdk-headless` (в системе его не было: ставится скриптом), `clang-22`/`lld-22`/`llvm-22` (`clang-21` нет), `aapt` с `aapt2` 2.19, `apksigner`, `zipalign`. `gradle` в Debian 4.4.1 — слишком старый (CI: 8.11.1, Java 17): Gradle берётся архивом с сайта, он на Java и от архитектуры не зависит.
- `adb` стоит (Termux, 35.0.2), но демон в PRoot не стартует (`proot error: 'tcp:5037' not found`).
- vcpkg собирается здесь (`~/vcpkg`, `VCPKG_FORCE_SYSTEM_BINARIES=1`): на Linux 145 пакетов ушли около 4 часов, на двух потоках `VCPKG_MAX_CONCURRENCY=2` (память: 11 ГБ, подкачка 12 ГБ принадлежит хосту).
- CI собирает NDK r30 (`30.0.16248370`, `clang` 21.0.0): версия видна по строкам `clang version` в `.comment` библиотек опубликованного APK.
- Google не выпускает NDK под aarch64 Linux, но sysroot x86_64-NDK от хоста не зависит: гибрид «официальный NDK + системный arm64 `clang`» собрал и запустил на устройстве C++23 (`docs/android-local-toolchain.md`); неофициальные сборки не понадобились.
- `aapt2` 2.19 из Debian не читает `android.jar` API 35; `aapt2` 16.0.0.4 из Termux читает (бинарник под bionic, PRoot его запускает).

## Принципы

- **Тот же результат, что у CI.** Те же триплет `arm64-android`, флаги (`-DANDROID_ABI=arm64-v8a`, `-DANDROID_PLATFORM=android-26`, `-DANDROID_STL=c++_shared`), цель `z13_android_stage` и `gradle assembleDebug` с теми же `-Pz13...`. Локальный скрипт повторяет шаги workflow, а не придумывает свои; расхождение — повод поправить оба.
- **Только Release для vcpkg.** Триплет уже ставит `VCPKG_BUILD_TYPE release`: вдвое меньше сборок, чем на Linux.
- **Отдельный каталог сборки** (`build-android/`), чтобы не трогать `build/` (Linux, без рендерера).
- **Скрипты на Python**, как `make.py` и скрипты `build-and-test`; долгие шаги — отделённо от сессии (`CLAUDE.md`, «Build & test»), с логом и строкой `EXIT`.
- **Лимиты PRoot.** `-j3` для проекта, `VCPKG_MAX_CONCURRENCY` не выше 2, сборки зависимостей — отделённо. Память: Release vcpkg с `-j2` и три компилятора проекта вместе не должны превышать ОЗУ; при `EXIT 137` — `-j2`.

## Порядок работ (одна ветка на этап)

1. **`f/android-local-toolchain`** — разведка и установка инструментов, без изменений кода: JDK 21 (Gradle 8.11.1 держит Java 17-21, проверить с AGP из `android/build.gradle`), Gradle 8.11.1 архивом, `cmdline-tools` и `platforms;android-35` (чистые данные, `android.jar`; `sdkmanager` — Java), NDK под aarch64 и `aapt2`. Проверки по одной: `clang --version` из NDK, `aapt2 version`, сборка пустого `.c` под `aarch64-linux-android26`. Итог — страница «как поставить» в `docs/` и скрипт `tools/android/setup_toolchain.py` (идемпотентный, с версиями и хэшами скачиваемого). Если NDK не запускается — решение: искать другую сборку или остановиться; остальные этапы без него не имеют смысла.
   Сделано (`f/android-local-toolchain`): `tools/android/setup_toolchain.py` и `docs/android-local-toolchain.md`. Отличия от замысла: NDK — официальный r30 с подменой x86_64-бинарников системным `clang-22` (а не неофициальная сборка); `aapt2` — из Termux (Debian-версия не читает API 35); `sdkmanager` не запускается, SDK (`platforms;android-35`, `build-tools;34.0.0`, `cmdline-tools`) разложен вручную по ссылкам из манифеста Google. Проверено: пробный C++23 собирается штатным `android.toolchain.cmake` и запускается на устройстве; `gradle assembleDebug` с пустой стадией даёт подписанный APK за 41 с. Установка с нуля — около трёх минут при прогретом кеше загрузок.
2. **`f/android-local-build`** (этапы 2 и 3 одной веткой: без собираемого APK библиотеки не нужны) — vcpkg для `arm64-android` локально: `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/...`, `ANDROID_NDK_HOME`, `VCPKG_CHAINLOAD_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake`. Хост-инструменты vcpkg (cmake, ninja) — системные. Скрипт `tools/android/build_libs.py` (конфигурация, `--target z13_android_stage`, отделённый запуск, лог). Замеряем: время, память, `ccache`. Ожидание: несколько часов для зависимостей, потом минуты на проект; кеш двоичных пакетов vcpkg сократит повторы.
   Сделано (`f/android-local-build`): `tools/android/build_libs.py`, раздел «Библиотеки проекта» в `docs/android-local-toolchain.md`. Замеры первой сборки: зависимости около 2,3 часа работы, проект с `-j3` 32 минуты; набор из 21 библиотеки и их размеры совпадают с APK из CI. Отличия от замысла: `VCPKG_INSTALLED_DIR` у Android свой (`build-android/vcpkg_installed`), `CMakeLists.txt` берёт его из `-D`, если задан; vcpkg по умолчанию на двух потоках (`--vcpkg-jobs`), а не на одном; телефону нужен `termux-wake-lock` (без него сборка стоит, пока он спит).
3. Упаковка (та же ветка): SDL Java-исходники (из релиза той же версии SDL3, как в workflow), `gradle -p android assembleDebug` с `-Pz13StageDir`, `-Pz13SdlJavaDir`, `-Pz13VersionCode`, `-Pz13VersionName`. AGP скачивает `aapt2` из Google Maven под x86 — подменяется `android.aapt2FromMavenOverride` на локальный (свойство Gradle в `~/.gradle/gradle.properties`, не в репозитории). Версия: `git rev-list --count $(git merge-base HEAD origin/main)` и `version.txt`, как в workflow. Скрипт `tools/android/build_apk.py`.
   Сделано (`f/android-local-build`): `tools/android/build_apk.py`, раздел «APK» в `docs/android-local-toolchain.md`. Gradle 31 с, APK 79,7 МБ (CI: 79,8), подпись v2 сходится; `aapt2` подменяется ключом `-P` в команде, а не файлом в `~/.gradle`. Установлен и запущен на телефоне.
4. **`f/android-local-run`** — установка и запуск на том же телефоне. Варианты (проверить, какой работает): (а) `termux-open` с APK — системный установщик, подтверждение вручную; (б) беспроводная отладка (`adb pair`/`adb connect localhost:<порт>`, Android 11+) и `adb install -r`, `adb shell am start -n org.zodiac13.game/...` — полностью по скрипту; (в) `adb` из Termux, не из PRoot, если PRoot не пробрасывает сокеты. Подпись — отладочным ключом из репозитория, так что новый APK встаёт поверх старого (`versionCode` растёт по числу коммитов). Скрипт `tools/android/install_run.py`; логи игры — `adb logcat`.
   Сделано (`f/android-local-build`): выбран путь (г): Termux не имеет права устанавливать приложения, поэтому `install_run.py` и виджет `install-zodiac13` открывают папку Загрузки в «Файлах», установка тапом по APK; `--launch` стартует игру. Игра установилась и запустилась. Подробности и что не сработало — в `docs/android-local-toolchain.md`. Логи игры `adb` не даёт (его нет), нужна проверка `logcat` из Termux (читается напрямую).
5. **Сведение в один вызов.** `python3 make.py --android` (или отдельная команда) со стадиями «библиотеки → APK → установка → запуск» и флагами пропуска; в `build-and-test` добавляется раздел про Android: когда локально, когда CI.
   Сделано частично (`f/android-local-build`): `tools/android/android.py` — библиотеки (отделённо, если нет стадии или `--libs`) → APK → установка (`--no-install`); `build_apk.py --install`. Не сделано: запуск игры в той же команде, ссылка из `make.py`, раздел в `build-and-test`.

Этапы последовательные: каждый нужен следующему; этап 1 решает, возможно ли остальное.

**Второе устройство (2026-11-13).** Инструменты поставлены `setup_toolchain.py --install-apt` без правок скриптов (раздел «Новое устройство: порядок с нуля» в `android-local-toolchain.md`); библиотеки (около 2 часов вместе с проектом) и APK (80,9 МБ) собраны, замер — в `android-local-toolchain.md`.

## Открытые вопросы

- Как следить за обновлением NDK вместе с `ANDROID_NDK_LATEST_HOME` в CI: версия и хэш закреплены в скрипте (процедура — в конце `android-local-toolchain.md`); расхождение компилятора (Debian `clang` 22 против 21 у Google) побитово совпадения не даёт.
- Лицензия Android SDK: скрипт её не принимает (`sdkmanager --licenses` не запускается), архивы берутся по ссылкам из манифеста Google, как в CI.
- Хватит ли ОЗУ и подкачки на Release-сборку тяжёлых портов (`assimp`, `bullet3`) для Android; `VCPKG_MAX_CONCURRENCY=1` — запасной вариант.
- Какой способ установки (этап 4) работает на устройстве пользователя: зависит от версии Android и включённой отладки по Wi-Fi.
- Подпись: отладочный ключ годится для личных тестов; публикуются по-прежнему только APK из CI.
- Нужен ли профиль `-DZ13_PROFILE` локально (в CI он включён вне релиза): да, как в workflow, иначе нечем снимать кадры на устройстве.
