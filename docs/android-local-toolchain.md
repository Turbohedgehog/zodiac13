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

## Обновление версий

NDK, Gradle, SDK и пакеты Termux закреплены в начале скрипта (ссылки и хэши). Новый NDK: версия из `ANDROID_NDK_LATEST_HOME` образа CI (читается по строке `clang version` в `.comment` библиотек опубликованного APK), хэш — в `repository2-3.xml` на `dl.google.com`; при смене мажорной версии `clang` в NDK меняется `NDK_CLANG_MAJOR` (каталог `lib/clang/<N>`). Пакеты Termux устаревают в пуле: если загрузка падает с 404, взять актуальные версии и хэши из индекса `packages.termux.dev/apt/termux-main/dists/stable/main/binary-aarch64/Packages`.
