#!/usr/bin/env python3
"""Starts a build, and the tests after it if asked for and the build succeeds, detached from
the calling shell. Writes build.log (and ctest.log), each ending in an "EXIT <code>" line, and
build.pid for stop_build.py.

Usage: build.py [--tests] [--keep-going] [--target TARGET] [--config CONFIG] [--build-dir DIR] [--log-dir DIR]
"""

import argparse
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_LOG_DIR = Path(tempfile.gettempdir()) / "z13-build"
# More parallel jobs have hung PRoot sessions (CLAUDE.md, "Build & test").
PROOT_JOBS = 3


def repo_root() -> Path:
    return Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                               check=True).stdout.strip())


def jobs() -> int:
    if "PRoot" in " ".join(platform.uname()):
        return PROOT_JOBS
    return os.cpu_count() or 1


def run_logged(command: list[str], log: Path, cwd: Path) -> int:
    with log.open("w") as out:
        code = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, cwd=cwd).returncode
        out.write(f"\nEXIT {code}\n")
    return code


def run(args: argparse.Namespace) -> int:
    repo = repo_root()
    build = ["cmake", "--build", args.build_dir, f"-j{jobs()}"]
    if args.target:
        build += ["--target", args.target]
    if args.config:
        build += ["--config", args.config]
    if args.keep_going:
        build += ["--", "-k", "0"]
    if run_logged(build, args.log_dir / "build.log", repo) != 0 or not args.tests:
        return 0
    ctest = ["ctest", "--test-dir", args.build_dir] + (["-C", args.config] if args.config else [])
    run_logged(ctest, args.log_dir / "ctest.log", repo)
    return 0


def start(args: argparse.Namespace) -> int:
    args.log_dir.mkdir(parents=True, exist_ok=True)
    for name in ("build.log", "ctest.log", "tests_requested"):
        (args.log_dir / name).unlink(missing_ok=True)
    if args.tests:
        (args.log_dir / "tests_requested").touch()
    command = [sys.executable, __file__, "--run"] + sys.argv[1:]
    detach = {"creationflags": subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP} \
        if os.name == "nt" else {"start_new_session": True}
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, **detach)
    (args.log_dir / "build.pid").write_text(str(process.pid))
    print(f"logs: {args.log_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", action="store_true")
    parser.add_argument("--keep-going", action="store_true", help="collect every compile error in one pass")
    parser.add_argument("--target")
    parser.add_argument("--config", help="for multi-config generators (Visual Studio)")
    parser.add_argument("--build-dir", default="build", help="build-release for make.py's Release build")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    parser.add_argument("--run", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    return run(args) if args.run else start(args)


if __name__ == "__main__":
    sys.exit(main())
