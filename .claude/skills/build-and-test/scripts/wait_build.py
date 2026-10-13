#!/usr/bin/env python3
"""Waits for a build started by build.py and summarizes it: the compiler errors, or the test
result and the failed tests. With --status, reports the progress so far without waiting.

Usage: wait_build.py [--status] [--log-dir DIR]
"""

import argparse
import re
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

DEFAULT_LOG_DIR = Path(tempfile.gettempdir()) / "z13-build"
POLL_SECONDS = 20
MAX_LINES = 40
# GCC/Clang "error:", MSVC ": error C2065:".
ERROR = re.compile(r"error:|: (fatal )?error [A-Z]+\d+")
FAILED_TEST = re.compile(r"^\s+\d+ - (?!.*(Disabled|Skipped))")


def read(path: Path) -> str:
    return path.read_text(errors="replace") if path.exists() else ""


def exit_code(log: str) -> int | None:
    found = re.search(r"^EXIT (-?\d+)$", log, re.M)
    return int(found.group(1)) if found else None


def errors(log: str) -> list[str]:
    lines = [re.sub(r".*[/\\](src[/\\])", r"\1", line) for line in log.splitlines() if ERROR.search(line)]
    failed = [line for line in log.splitlines() if line.startswith("FAILED:")]
    return (sorted(set(lines)) + failed)[:MAX_LINES]


def status(log_dir: Path) -> int:
    build = read(log_dir / "build.log")
    steps = re.findall(r"^\[\d+/\d+\]", build, re.M)
    print(f"build: {steps[-1] if steps else 'not started'} exit={exit_code(build)}")
    print(f"errors so far: {sum(1 for line in build.splitlines() if ERROR.search(line))}")
    print("\n".join(errors(build)))
    ctest = read(log_dir / "ctest.log")
    if ctest:
        print(f"tests run: {len(re.findall(r'Test +#', ctest))} exit={exit_code(ctest)}")
        print("\n".join(line for line in ctest.splitlines() if "***Failed" in line or "***Exception" in line))
    print(f"time: {datetime.now():%H:%M:%S}")
    return 0


def wait(log: Path) -> str:
    while exit_code(read(log)) is None:
        time.sleep(POLL_SECONDS)
    return read(log)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    args = parser.parse_args()
    if args.status:
        return status(args.log_dir)
    build = wait(args.log_dir / "build.log")
    if exit_code(build) != 0:
        print("BUILD FAILED")
        print("\n".join(errors(build)))
        return 1
    print("build ok")
    if not (args.log_dir / "tests_requested").exists():
        return 0
    ctest = wait(args.log_dir / "ctest.log")
    for line in ctest.splitlines():
        if "tests passed" in line or "tests failed" in line or FAILED_TEST.match(line):
            print(line)
    return 0 if exit_code(ctest) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
