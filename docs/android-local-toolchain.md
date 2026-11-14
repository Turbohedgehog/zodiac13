# Локальная сборка APK: инструменты

Что нужно, чтобы собирать APK на aarch64 Linux (PRoot Debian под Termux), где Google не выпускает NDK и `aapt2`. План — `android-local-build-plan.md`, этап 1. Ставит всё `tools/android/setup_toolchain.py`; всё лежит вне репозитория, в `~/android` (`--root`).

## Установка

```
python3 tools/android/setup_toolchain.py --install-apt
```

`--install-apt` ставит через `sudo` недостающие пакеты Debian: `clang-22 lld-22 llvm-22 openjdk-21-jdk-headless unzip`. Без него скрипт печатает команду и останавливается. Остальное скрипт делает сам и каждый шаг пропускает, если сделан: можно запускать повторно после обрыва. Скачанное проверяется по хэшам, зашитым в скрипт; архивы остаются в `~/android/downloads`. Свежая установка при прогретом кеше — около трёх минут, с нуля — загрузка около 1,1 ГБ.

В конце скрипт собирает пробный C++23 под Android, запускает Gradle и `aapt2`; `--verify-only` делает только эту проверку.

## Что где

| Каталог в `~/android` | Что |
| --- | --- |
| `ndk/android-ndk-r30` | официальный NDK r30 (`30.0.16248370`), x86_64-бинарники подменены (ниже) |
| `gradle/gradle-8.11.1` | Gradle, как в CI (он на Java, от архитектуры не зависит) |
| `sdk` | `platforms/android-35`, `build-tools/34.0.0` (умолчание AGP 8.7.3), `cmdline-tools/latest` |
| `termux-aapt2`, `bin/aapt2` | `aapt2` из Termux и обёртка с `LD_LIBRARY_PATH` |
| `downloads` | скачанные архивы |

## Как это устроено

- **Компилятор.** Официальный NDK для Linux — только x86_64, а его sysroot (заголовки, libc, libc++, `crt*.o`, рантаймы) от архитектуры хоста не зависит. Бинарники LLVM в `toolchains/llvm/prebuilt/linux-x86_64/bin` заменены системными: `clang-21` — скрипт, вызывающий `/usr/lib/llvm-22/bin/clang` с `-resource-dir`, `--sysroot`, `--rtlib=compiler-rt --unwindlib=libunwind` и `-fuse-ld=lld` (для `clang++` ещё `-stdlib=libc++`); `lld` и `llvm-*` — ссылки. Оригиналы остались рядом как `*.x86_64.orig`. Поэтому штатный `android.toolchain.cmake` NDK и триплет `arm64-android` работают без изменений.
- **`aapt2`.** AGP тянет `aapt2` из Google Maven под x86_64. Подставляется свой: `-Pandroid.aapt2FromMavenOverride=~/android/bin/aapt2`. `aapt2` из Debian (2.19, Android 14) не читает `android.jar` для API 35 (`RES_TABLE_TYPE_TYPE entry offsets overlap actual entry data`); из Termux (16.0.0.4, Android 16) читает. Это бинарник под bionic: PRoot его запускает, так как `/system/bin/linker64` виден, библиотеки берутся из распакованных пакетов Termux (9 штук, закреплены в скрипте).
- **SDK** разложен вручную: `sdkmanager` из `cmdline-tools` в этой версии — обёртка над x86_64-бинарником `android` и не запускается. Лицензию Android SDK скрипт не принимает: он скачивает те же архивы по ссылкам из манифеста Google, что и CI.
- **`curl -4`.** IPv6 до этих хостов в PRoot не проходит (таймаут), поэтому загрузки — по IPv4.

## Отличия от CI

- Компилятор — `clang` 22.1.8 из Debian, а в CI — `clang` 21.0.0 сборки Google (у Debian нет `clang-21`). Библиотеки `libc++`, libc и рантаймы те же (из NDK r30), а машинный код может отличаться, побитово APK не совпадёт.
- `aapt2` другой версии, чем у AGP 8.7.3 (`8.7.3-12006047`).

## Как пользоваться

```
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-arm64
export ANDROID_HOME=~/android/sdk
export ANDROID_NDK_HOME=~/android/ndk/android-ndk-r30
~/android/gradle/gradle-8.11.1/bin/gradle -p android assembleDebug --no-daemon \
  -Pz13StageDir=<стадия z13_android_stage> -Pz13SdlJavaDir=<Java-исходники SDL> \
  -Pz13VersionCode=1 -Pz13VersionName=0.0.0-local \
  -Pandroid.aapt2FromMavenOverride=$HOME/android/bin/aapt2
```

Стадия и Java-исходники SDL — как в `.github/workflows/android.yml`; сборка библиотек и скрипт упаковки — этапы 2 и 3 плана. Проверено 2026-11-10: с пустой стадией и SDL 3.4.14 APK (60 КБ, подпись v2 сходится) собирается за 41 с.

## Библиотеки проекта

```
python3 tools/android/build_libs.py          # отделённо; лог и build.pid — в /tmp/z13-build-android
python3 .claude/skills/build-and-test/scripts/wait_build.py --status --log-dir /tmp/z13-build-android
```

Скрипт повторяет настройку из `.github/workflows/android.yml` (триплет `arm64-android`, NDK, `android-26`, `c++_shared`, `-DZ13_PROFILE=ON`, `ccache`), настраивает проект в `build-android/` (vcpkg ставит зависимости во время настройки) и собирает `z13_android_stage`: библиотеки и ресурсы для Gradle лежат в `build-android/android/`. `--bin-dir` (по умолчанию `android_bin_release/`: библиотеки и исполняемый файл Android не лежат в `bin/` Linux-сборки, иначе затирают её `libz13_core.so`), `--vcpkg-jobs` (по умолчанию 2), `--jobs` (3), `--configure-only`, `--build-dir`.

- **Отдельное дерево vcpkg.** Скрипт передаёт `-DVCPKG_INSTALLED_DIR=build-android/vcpkg_installed`. Общее `vcpkg_installed/` в корне — дерево Linux-сборки: пакеты двух триплетов в одном дереве vcpkg снимает друг у друга (при первой попытке за 30 секунд слетели 140 пакетов `arm64-linux`; их вернула настройка `build/` из бинарного кеша за 2,5 минуты, а датам файлов пришлось вернуть старое значение, иначе Ninja пересобирал все 573 шага).
- **Телефон не должен спать.** Остановленный сном телефона процесс не продвигается: для `assimp` часы Ninja показали 27 минут при 325 на стене. Запуск сессии берёт `termux-wake-lock` (`~/.shortcuts/debian-th`).

Замер 2026-11-10/11 (Release, первая сборка, `ccache` пуст): 125 пакетов vcpkg — около 2,3 часа работы (первые 110 пакетов на одном потоке — 130 минут, из них `assimp` 56; последние пакеты и настройка проекта на двух потоках — 8 минут), проект `z13_android_stage` с `-j3` — 32 минуты; сюда не входят пять часов простоя, пока телефон спал. В `build-android/` потом 608 МБ в `vcpkg_installed`, 131 МБ в стадии (21 библиотека и 74 МБ ресурсов); набор библиотек и размеры совпадают с APK из CI.

## APK

```
python3 tools/android/build_apk.py       # после build_libs.py; около минуты, лог — в терминал
```

Скрипт берёт Java-исходники SDL того же выпуска, что поставил vcpkg (3.4.14; кладёт в `~/android/sdl-java`), запускает `gradle assembleDebug` с теми же `-Pz13...`, что workflow, и `aapt2` из Termux, и кладёт APK в `build-android/apk/zodiac13-<версия>-local-android-arm64.apk`. Версия — `version.txt`, число коммитов в `main` до точки ветвления и суффикс `-local`.

Замер 2026-11-11: Gradle 31 с (всего 37 с), APK 79,7 МБ (у CI на тот же момент 79,8 МБ), 21 библиотека `arm64-v8a`, подпись v2 сходится. На телефон не ставился (этап 4).

## Одной командой

```
python3 tools/android/android.py               # APK и установка (около полуминуты при готовых библиотеках)
python3 tools/android/android.py --libs        # заново библиотеки: отделённо, затем повторить без флага
python3 tools/android/android.py --no-install  # только APK
```

Без собранной стадии (`build-android/android/jniLibs`) скрипт запускает `build_libs.py` отделённо и останавливается. `build_apk.py --install` собирает и ставит, `install_run.py` ставит последний APK.

## Установка на телефон

```
python3 tools/android/install_run.py            # копия в /sdcard/Download и открытая папка «Файлы»: тап по APK
python3 tools/android/install_run.py --launch   # запуск установленной игры
```

То же делает ярлык Termux:Widget «Install Zodiac13»: скрипт `tools/android/termux/Install Zodiac13` копируют в `~/.shortcuts/` Termux (это `/data/data/com.termux/files/home/.shortcuts`, из PRoot виден; на втором устройстве положен 2026-11-13 рядом с «Debian (th)» и «Debian Desktop») и делают исполняемым. Он вызывает `install_run.py` внутри PRoot через `proot-distro login debian --user th`, как соседний ярлык «Debian (th)». В виджете запустить из PRoot его нельзя (вложенный `proot` не стартует), поэтому проверена только внутренняя команда; сам ярлык проверяется тапом в Termux:Widget.

Termux сам установить APK не может: у него нет `REQUEST_INSTALL_PACKAGES` (пункта «Установка неизвестных приложений» в настройках нет), и системный установщик, запущенный из него, закрывается без окна и без записи в логе. Не сработали (2026-11-11): `termux-open` (приёмник Termux молча ничего не запускает; с `--chooser` предлагает только сохранить файл), `am start` с `file://` (Termux подхватывает его сам: «open directory / edit / cancel»), `am start -n com.android.packageinstaller/.InstallStart`. Работает открытая папка «Файлы»: у менеджера разрешение на установку есть. APK подписан `android/debug.keystore` из репозитория, как в CI, так что встаёт поверх прежнего. Беспроводной `adb` не нужен: в PRoot демон стартует только как `adb -L tcp:5037 server nodaemon`, но сопряжение настраивается на каждом устройстве.

## Новое устройство: порядок с нуля

Проверено 2026-11-13 на втором телефоне (PRoot Debian под Termux, aarch64, 8 ядер, 10 ГБ ОЗУ, 206 ГБ свободно; у пользователя `sudo` без пароля). Всё из репозитория, ничего не придумывать заново:

1. **Linux-сторона** (нужна `VCPKG_ROOT`, `build-and-test/references/proot.md`): `~/vcpkg` с `bootstrap-vcpkg.sh`; `export VCPKG_FORCE_SYSTEM_BINARIES=1`.
2. **Инструменты:** `python3 tools/android/setup_toolchain.py --install-apt` (apt: `clang-22 lld-22 llvm-22 openjdk-21-jdk-headless unzip`; затем NDK r30, Gradle 8.11.1, SDK, пакеты Termux для `aapt2`, около 4 ГБ в `~/android`, заняло несколько минут, 14 загрузок). Скрипт сам проверяет себя: пробный C++23 под Android, Gradle, `aapt2` 2.20 (Android 16). Повторный запуск безопасен.
3. **Библиотеки** (долго, 2-3 часа на двух потоках; vcpkg ставит `arm64-android` во время настройки): отделённо, с телефоном, который не спит. `termux-wake-lock` в Termux до запуска (ярлык «Debian (th)» в Termux:Widget на втором устройстве — только `exec proot-distro login debian --user th`, блокировку не берёт: вызвать `termux-wake-lock` в Termux до входа или дописать её первой строкой в ярлык; файлы ярлыков лежат в `/data/data/com.termux/files/home/.shortcuts`); `python3 tools/android/build_libs.py`, состояние — `python3 .claude/skills/build-and-test/scripts/wait_build.py --status --log-dir /tmp/z13-build-android`.
4. **APK:** `python3 tools/android/build_apk.py` (или `android.py`, см. «Одной командой»): около минуты при готовых библиотеках.
5. **Установка:** `python3 tools/android/install_run.py`. Нужно: `termux-setup-storage` в Termux один раз (`/sdcard/Download` виден из PRoot, проверено), `am` из Termux в `PATH` (виден), «Файлы» с правом установки (тап по APK). Ярлык `~/.shortcuts/install-zodiac13` (Termux:Widget) — необязательный: лежит вне репозитория, вызывает `install_run.py` в PRoot. Без Termux:Widget достаточно команды в сессии.

Что на новом устройстве было пусто и создаётся скриптами: `~/android` (весь инструментарий), `build-android/`, `~/android/sdl-java`. Что делается вручную: Termux-доступ к памяти, лаунчер с `termux-wake-lock`, права «Файлов» на установку. `adb` нужен только Termux-вариант и только для логов (`logcat`).

Замер 2026-11-13 на втором телефоне (Release, `ccache` и кеш vcpkg пусты, `termux-wake-lock` взят): 127 пакетов vcpkg с 13:03 до 14:40 (около 97 минут: `assimp`, `bullet3`, `sdl3` самые долгие), проект `z13_android_stage` 30 минут (434 шага), `build_apk.py` 1 мин 52 с, APK `zodiac13-0.3.517-local-android-arm64.apk` 80,9 МБ, 23 библиотеки в `jniLibs/arm64-v8a`. Шаг 5 пройден: `install_run.py` положил APK в Загрузки и открыл «Файлы», APK установлен тапом и игра запущена (2026-11-13). «Файлы» на Xiaomi не сразу показали новый файл: помогли обновление списка (потянуть вниз) и открытие `Download` по пути, а не через раздел «Загрузки»; рассылка `MEDIA_SCANNER_SCAN_FILE` через `am broadcast` без ошибок, но её эффект не проверялся.

## Обновление версий

NDK, Gradle, SDK и пакеты Termux закреплены в начале скрипта (ссылки и хэши). Новый NDK: версия из `ANDROID_NDK_LATEST_HOME` образа CI (читается по строке `clang version` в `.comment` библиотек опубликованного APK), хэш — в `repository2-3.xml` на `dl.google.com`; при смене мажорной версии `clang` в NDK меняется `NDK_CLANG_MAJOR` (каталог `lib/clang/<N>`). Пакеты Termux устаревают в пуле: если загрузка падает с 404, взять актуальные версии и хэши из индекса `packages.termux.dev/apt/termux-main/dists/stable/main/binary-aarch64/Packages`.
