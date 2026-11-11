#!/usr/bin/env python3
"""Packs the stage of build_libs.py into a debug APK with Gradle, as the "Build the APK" step of
.github/workflows/android.yml does. Needs the toolchain of setup_toolchain.py and a finished build_libs.py run.

The APK lands in <build-dir>/apk. Gradle takes a few minutes in PRoot: run it detached (build-and-test skill).

Usage: build_apk.py [--root DIR] [--build-dir DIR] [--install]
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from build_libs import TRIPLET, VERSION_SUFFIX, BuildError, repo_root, version_patch
from install_run import InstallError, install
from setup_toolchain import GRADLE_VERSION, JAVA_HOME, default_root, log, ndk_home

SDL_JAVA_SUBPATH = "android-project/app/src/main/java"


def sdl_version(installed: Path) -> str:
    """The SDL3 version vcpkg installed, read as the workflow does."""
    config = installed / TRIPLET / "share/sdl3/SDL3ConfigVersion.cmake"
    for line in config.read_text().splitlines():
        if line.startswith('set(PACKAGE_VERSION "'):
            return line.split('"')[1]
    raise BuildError(f"no PACKAGE_VERSION in {config}")


def fetch_sdl_java(root: Path, version: str) -> Path:
    """SDLActivity's Java sources of the release matching vcpkg's SDL3 (its port does not install them)."""
    target = root / "sdl-java" / version
    java_dir = target / SDL_JAVA_SUBPATH
    if java_dir.exists():
        return java_dir
    target.mkdir(parents=True, exist_ok=True)
    url = f"https://github.com/libsdl-org/SDL/archive/refs/tags/release-{version}.tar.gz"
    log(f"fetching SDL {version} Java sources")
    download = subprocess.run(["curl", "-4", "-sSfL", url], stdout=subprocess.PIPE, check=True).stdout
    subprocess.run(["tar", "xz", "-C", str(target), "--strip-components=1",
                    f"SDL-release-{version}/{SDL_JAVA_SUBPATH}"], input=download, check=True)
    return java_dir


def build(args: argparse.Namespace) -> Path:
    repo = repo_root()
    stage = repo / args.build_dir / "android"
    if not (stage / "jniLibs").is_dir():
        raise BuildError(f"no stage in {stage}: run tools/android/build_libs.py first")
    code = version_patch(repo)
    name = f"{(repo / 'version.txt').read_text().strip()}.{code}{VERSION_SUFFIX}"
    java_dir = fetch_sdl_java(args.root, sdl_version(repo / args.build_dir / "vcpkg_installed"))
    gradle = args.root / "gradle" / f"gradle-{GRADLE_VERSION}" / "bin" / "gradle"
    environment = {**os.environ, "JAVA_HOME": str(JAVA_HOME), "ANDROID_HOME": str(args.root / "sdk"),
                   "ANDROID_NDK_HOME": str(ndk_home(args.root))}
    # AGP's aapt2 from Google Maven is x86_64 only (docs/android-local-toolchain.md).
    command = [str(gradle), "-p", "android", "assembleDebug", "--no-daemon", f"-Pz13StageDir={stage}",
               f"-Pz13SdlJavaDir={java_dir}", f"-Pz13VersionCode={code}", f"-Pz13VersionName={name}",
               f"-Pandroid.aapt2FromMavenOverride={args.root / 'bin/aapt2'}"]
    log("gradle assembleDebug")
    result = subprocess.run(command, cwd=repo, env=environment)
    if result.returncode != 0:
        raise BuildError(f"gradle finished with {result.returncode}")
    apk_dir = repo / args.build_dir / "apk"
    apk_dir.mkdir(parents=True, exist_ok=True)
    apk = apk_dir / f"zodiac13-{name}-android-arm64.apk"
    (repo / "android/app/build/outputs/apk/debug/app-debug.apk").replace(apk)
    log(f"APK: {apk} ({apk.stat().st_size / 1e6:.1f} MB)")
    return apk


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=default_root(), help="where setup_toolchain.py installed")
    parser.add_argument("--build-dir", default="build-android", help="the one of build_libs.py")
    parser.add_argument("--install", action="store_true", help="then open it in the file manager (install_run.py)")
    args = parser.parse_args()
    try:
        apk = build(args)
        if args.install:
            install(apk)
        return 0
    except (BuildError, InstallError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
