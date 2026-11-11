#!/usr/bin/env python3
"""One entry point of the local Android build: the libraries (build_libs.py, hours, detached) when the stage is
missing or --libs asks, then the APK (build_apk.py) and its install (install_run.py) with the file manager.

Without a stage it starts the library build and stops: wait for it (wait_build.py --log-dir /tmp/z13-build-android),
then run this again.

Usage: android.py [--libs] [--no-install] [--build-dir DIR]
"""

import argparse
import subprocess
import sys
from pathlib import Path

from build_apk import build
from build_libs import BuildError
from install_run import InstallError, install
from setup_toolchain import default_root

TOOLS = Path(__file__).resolve().parent


def start_libs() -> None:
    subprocess.run([sys.executable, str(TOOLS / "build_libs.py")], check=True)
    print("libraries are building detached; wait for EXIT in the log, then run this again")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--libs", action="store_true", help="rebuild the libraries too (detached, then stop)")
    parser.add_argument("--no-install", action="store_true", help="stop after the APK")
    parser.add_argument("--build-dir", default="build-android")
    args = parser.parse_args()
    args.root = default_root()
    try:
        if args.libs or not Path(args.build_dir, "android/jniLibs").is_dir():
            start_libs()
            return 0
        apk = build(args)
        if not args.no_install:
            install(apk)
        return 0
    except (BuildError, InstallError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
