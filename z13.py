#!/usr/bin/env python3
"""Runs what make.py built: the game (default), a dedicated server, the tests or the benchmarks.

Arguments after `--` go to the game, the server or the test runner as they are, e.g.
`python3 z13.py --server 26214 -- --fps 30`.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
GAME_DIRS = {"build": ROOT / "bin", "install": ROOT / "install"}
GAME_NAME = "zodiac13"
TEST_RUNNER = ROOT / "bin" / "tests" / "z13_test_runner"
# Benchmarks are disabled gtests, built only by `make.py --bench`.
DEFAULT_BENCH_FILTER = "*.DISABLED_*"


def executable(directory: Path, name: str) -> Path:
  path = directory / (name + (".exe" if sys.platform == "win32" else ""))
  if not path.exists():
    sys.exit(f"{path} not found: build it first with make.py")
  return path


def run(command: list, cwd: Path) -> int:
  print(" ".join(str(part) for part in command), flush=True)
  return subprocess.call([str(part) for part in command], cwd=cwd)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  mode = parser.add_mutually_exclusive_group()
  mode.add_argument("--game", action="store_true", help="run the game (default)")
  mode.add_argument("--server", nargs="?", const=0, type=int, metavar="PORT",
                    help="run a dedicated server, on PORT or the default port")
  mode.add_argument("--tests", action="store_true", help="run the tests (ctest, or the runner with --filter)")
  mode.add_argument("--bench", action="store_true", help="run the benchmarks (needs make.py --bench)")
  parser.add_argument("--filter", help="gtest filter for --tests and --bench")
  parser.add_argument("--from", dest="source", choices=sorted(GAME_DIRS), default="build",
                      help="run the game or server from bin/ (build, default) or install/")
  parser.add_argument("extra", nargs=argparse.REMAINDER, help="arguments after -- are passed through")
  options = parser.parse_args()
  extra = options.extra[1:] if options.extra[:1] == ["--"] else options.extra

  if options.tests:
    if options.filter:
      return run([executable(TEST_RUNNER.parent, TEST_RUNNER.name), f"--gtest_filter={options.filter}", *extra],
                 TEST_RUNNER.parent)
    return run(["ctest", "--test-dir", BUILD_DIR, "--output-on-failure", *extra], ROOT)
  if options.bench:
    return run([executable(TEST_RUNNER.parent, TEST_RUNNER.name), "--gtest_also_run_disabled_tests",
                f"--gtest_filter={options.filter or DEFAULT_BENCH_FILTER}", *extra], TEST_RUNNER.parent)

  game_dir = GAME_DIRS[options.source]
  game = executable(game_dir, GAME_NAME)
  if options.server is not None:
    server = ["--server"] + ([str(options.server)] if options.server else [])
    return run([game, *server, *extra], game_dir)
  return run([game, *extra], game_dir)


if __name__ == "__main__":
  sys.exit(main())
