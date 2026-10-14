#!/usr/bin/env python3

import os
import argparse
from pathlib import Path
from enum import Enum
import multiprocessing
import platform
import shutil
from dataclasses import dataclass
from typing import Optional

class BuildType(Enum):
  RELEASE = "Release"
  DEBUG = "Debug"

  def __str__(self):
    return self.value

ROOT = Path(__file__).resolve().parent


@dataclass(frozen=True)
class Layout:
  build_dir: Path
  bin_dir: Path
  # Only Debug installs: a Release build must not overwrite what install/ holds.
  install_dir: Optional[Path]


LAYOUTS = {
  BuildType.DEBUG: Layout(ROOT / "build", ROOT / "bin", ROOT / "install"),
  BuildType.RELEASE: Layout(ROOT / "build-release", ROOT / "bin-release", None),
}
# More parallel jobs have hung builds inside PRoot (see CLAUDE.md).
PROOT_MAX_JOBS = 3

def default_jobs() -> int:
  jobs = multiprocessing.cpu_count()
  return min(jobs, PROOT_MAX_JOBS) if "PRoot" in platform.release() else jobs

###  sudo chown -R $USER:$USER /opt/vcpkg

def on_off(enabled: bool) -> str:
  return "ON" if enabled else "OFF"

def generate_build_command(build_type: BuildType, bench: bool = False, profile: bool = False, jobs: int = 0) -> str:
  layout = LAYOUTS[build_type]
  # Ninja when installed, as for build/: header_width.py reads its dependency log.
  generator = "-G Ninja " if shutil.which("ninja") and not (layout.build_dir / "CMakeCache.txt").exists() else ""
  command = (
    f'cmake {generator}-S "{ROOT}" -B "{layout.build_dir}" -DCMAKE_BUILD_TYPE={build_type.value} '
    f'-DZ13_BUILD_BENCHMARKS={on_off(bench)} -DZ13_PROFILE={on_off(profile)} -DZ13_BIN_DIR="{layout.bin_dir.as_posix()}" && '
    f'cmake --build "{layout.build_dir}" -j {jobs or default_jobs()} --config {build_type.value}')
  if layout.install_dir:
    command += f' && cmake --install "{layout.build_dir}" --config {build_type.value} --prefix "{layout.install_dir}"'
  return command

def build(build_type: BuildType, bench: bool = False, profile: bool = False, jobs: int = 0):
  os.system(generate_build_command(build_type, bench, profile, jobs))

def run_tests(build_type: BuildType):
  cmd = (f'ctest --test-dir "{LAYOUTS[build_type].build_dir}" -C {build_type.value} --output-on-failure '
         f'--parallel {default_jobs()}')
  os.system(cmd)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-b", "--build", action="store_true", help="debug build zodiac 13")
  parser.add_argument("-br", "--build-release", action="store_true",
                      help="release build zodiac 13, into build-release/ and bin-release/ (the default)")
  parser.add_argument("-t", "--tests", action="store_true", help="run all tests")
  parser.add_argument("--bench", action="store_true",
                      help="also build the benchmark tests (tests/bench/); run them with z13.py --bench")
  parser.add_argument("--profile", action="store_true",
                      help="compile in the Tracy zones (Z13_PROFILE); see the perf-measure skill")
  parser.add_argument("-j", "--jobs", type=int, default=0,
                      help=f"parallel build jobs (default: all cores, at most {PROOT_MAX_JOBS} inside PRoot)")
  options = parser.parse_args()
  if options.tests:
    run_tests(BuildType.DEBUG if options.build else BuildType.RELEASE)
  elif options.build:
    build(BuildType.DEBUG, options.bench, options.profile, options.jobs)
  else:
    build(BuildType.RELEASE, options.bench, options.profile, options.jobs)

if __name__ == "__main__":
  main()