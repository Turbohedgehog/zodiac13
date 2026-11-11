#!/usr/bin/env python3
"""Copies the APK of build_apk.py to the shared Downloads and opens that folder in the file manager, where a tap
on the APK installs it (Termux itself may not install apps: no REQUEST_INSTALL_PACKAGES, so the installer closes
at once). With --launch it starts the installed game.

Usage: install_run.py [--build-dir DIR] [--apk FILE] [--launch]
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The shared storage: the file manager sees it, Termux's home it does not.
SHARED_DIRECTORY = Path("/sdcard/Download")
FOLDER_TYPE = "vnd.android.document/directory"
FOLDER_URI = "content://com.android.externalstorage.documents/document/primary%3ADownload"
ACTIVITY = "org.zodiac13.game/org.libsdl.app.SDLActivity"


class InstallError(Exception):
    pass


def newest_apk(build_dir: Path) -> Path:
    apks = sorted((build_dir / "apk").glob("*.apk"), key=lambda path: path.stat().st_mtime)
    if not apks:
        raise InstallError(f"no APK in {build_dir / 'apk'}: run tools/android/build_apk.py first")
    return apks[-1]


def tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise InstallError(f"{name} not found: it comes with Termux")
    return path


def install(apk: Path) -> None:
    if not SHARED_DIRECTORY.is_dir():
        raise InstallError(f"{SHARED_DIRECTORY} is not visible: run termux-setup-storage in Termux")
    copy = SHARED_DIRECTORY / apk.name
    shutil.copyfile(apk, copy)
    subprocess.run([tool("am"), "start", "--user", "0", "-a", "android.intent.action.VIEW", "-t", FOLDER_TYPE,
                    "-d", FOLDER_URI], check=True)
    print(f"{copy.name} is in the Downloads folder, now open: tap it to install")


def launch() -> None:
    subprocess.run([tool("am"), "start", "-n", ACTIVITY], check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=Path("build-android"), help="the one of build_libs.py")
    parser.add_argument("--apk", type=Path, help="default: the newest in <build-dir>/apk")
    parser.add_argument("--launch", action="store_true", help="start the installed game instead of installing")
    args = parser.parse_args()
    try:
        if args.launch:
            launch()
        else:
            install(args.apk or newest_apk(args.build_dir))
    except InstallError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
