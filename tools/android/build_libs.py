#!/usr/bin/env python3
"""Configures the project for Android and builds the target z13_android_stage (the libraries the APK packs),
detached from the calling shell. vcpkg builds the dependencies for arm64-android during the configure, which
takes hours in PRoot. The steps repeat .github/workflows/android.yml.

Like build.py, it writes build.log (one EXIT line at the end) and build.pid into --log-dir, so wait_build.py
and stop_build.py of the build-and-test skill work on it. Needs the toolchain of setup_toolchain.py and VCPKG_ROOT.

Usage: build_libs.py [--root DIR] [--build-dir DIR] [--bin-dir DIR] [--log-dir DIR] [--vcpkg-jobs N] [--jobs N] [--configure-only]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

from setup_toolchain import default_root, ndk_home

DEFAULT_LOG_DIR = Path(tempfile.gettempdir()) / "z13-build-android"
TRIPLET = "arm64-android"
# Two jobs ran a 107-minute vcpkg build and this script's last ports in PRoot without a failure (proot.md);
# CLAUDE.md asks for one on a full rebuild: --vcpkg-jobs 1.
DEFAULT_VCPKG_JOBS = 2
# The project's own compilers: more have hung PRoot sessions (CLAUDE.md).
DEFAULT_JOBS = 3
# Apart from bin/ and bin-release/: both Linux builds link the same library names there.
DEFAULT_BIN_DIR = "android_bin_release"
# Local APKs carry this suffix, so they are not taken for the CI's of the same commit.
VERSION_SUFFIX = "-local"


class BuildError(Exception):
    pass


def repo_root() -> Path:
    return Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                               check=True).stdout.strip())


def version_patch(repo: Path) -> str:
    """main's commit count up to the merge base, the versionCode of CI's builds (android.yml)."""
    base = subprocess.run(["git", "merge-base", "HEAD", "origin/main"], capture_output=True, text=True,
                          check=True, cwd=repo).stdout.strip()
    return subprocess.run(["git", "rev-list", "--count", base], capture_output=True, text=True, check=True,
                          cwd=repo).stdout.strip()


def configure_command(args: argparse.Namespace, repo: Path, ndk: Path, vcpkg_root: Path) -> list[str]:
    command = [
        "cmake", "-S", ".", "-B", args.build_dir, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_root / 'scripts/buildsystems/vcpkg.cmake'}",
        "-DCMAKE_BUILD_TYPE=Release", f"-DVCPKG_TARGET_TRIPLET={TRIPLET}", "-DVCPKG_BUILD_TYPE=release",
        f"-DVCPKG_INSTALLED_DIR={repo / args.build_dir / 'vcpkg_installed'}",
        f"-DZ13_BIN_DIR={repo / args.bin_dir}",
        f"-DZ13_BUILD_DATE={datetime.now(timezone.utc):%Y-%m-%d}",
        f"-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
        "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_shared",
        f"-DZ13_VERSION_PATCH={version_patch(repo)}", f"-DZ13_VERSION_SUFFIX={VERSION_SUFFIX}",
        "-DZ13_PROFILE=ON",
    ]
    if shutil.which("ccache"):
        command += ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    return command


def build_command(args: argparse.Namespace) -> list[str]:
    return ["cmake", "--build", args.build_dir, "--target", "z13_android_stage", f"-j{args.jobs}"]


def step(name: str, command: list[str], out, repo: Path, environment: dict[str, str]) -> int:
    started = time.monotonic()
    out.write(f"== {name}: {' '.join(command)}\n== started {datetime.now():%F %T}\n")
    out.flush()
    code = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, cwd=repo, env=environment).returncode
    out.write(f"\n== {name} finished with {code} after {time.monotonic() - started:.0f} s\n")
    out.flush()
    return code


def environment(args: argparse.Namespace, ndk: Path) -> dict[str, str]:
    return {**os.environ, "ANDROID_NDK_HOME": str(ndk), "VCPKG_MAX_CONCURRENCY": str(args.vcpkg_jobs),
            "VCPKG_FORCE_SYSTEM_BINARIES": "1"}  # vcpkg has no prebuilt tools for ARM hosts


def run(args: argparse.Namespace) -> int:
    repo, ndk = repo_root(), ndk_home(args.root)
    vcpkg_root = Path(os.environ["VCPKG_ROOT"])
    env = environment(args, ndk)
    with (args.log_dir / "build.log").open("w") as out:
        code = step("configure", configure_command(args, repo, ndk, vcpkg_root), out, repo, env)
        if code == 0 and not args.configure_only:
            code = step("build", build_command(args), out, repo, env)
        out.write(f"\nEXIT {code}\n")
    return 0


def preflight(args: argparse.Namespace) -> None:
    if "VCPKG_ROOT" not in os.environ:
        raise BuildError("VCPKG_ROOT is not set")
    if not (ndk_home(args.root) / "build/cmake/android.toolchain.cmake").exists():
        raise BuildError(f"no NDK under {args.root}: run tools/android/setup_toolchain.py first")


def start(args: argparse.Namespace) -> int:
    try:
        preflight(args)
    except BuildError as error:
        print(error, file=sys.stderr)
        return 1
    args.log_dir.mkdir(parents=True, exist_ok=True)
    (args.log_dir / "build.log").unlink(missing_ok=True)
    command = [sys.executable, __file__, "--run"] + sys.argv[1:]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, start_new_session=True)
    (args.log_dir / "build.pid").write_text(str(process.pid))
    print(f"logs: {args.log_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=default_root(), help="where setup_toolchain.py installed")
    parser.add_argument("--build-dir", default="build-android", help="apart from build/, the Linux one")
    parser.add_argument("--bin-dir", default=DEFAULT_BIN_DIR, help="where the libraries and the executable link to")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    parser.add_argument("--vcpkg-jobs", type=int, default=DEFAULT_VCPKG_JOBS)
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--configure-only", action="store_true", help="stop after the vcpkg install")
    parser.add_argument("--run", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    return run(args) if args.run else start(args)


if __name__ == "__main__":
    sys.exit(main())
