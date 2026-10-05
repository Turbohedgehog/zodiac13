#!/usr/bin/env python3

import os
import argparse
from pathlib import Path
from enum import Enum
import multiprocessing
import platform

class BuildType(Enum):
  RELEASE = "Release"
  DEBUG = "Debug"

  def __str__(self):
    return self.value

BUILD_DIR_NAME = "build"
# More parallel jobs have hung builds inside PRoot (see CLAUDE.md).
PROOT_MAX_JOBS = 3

def default_jobs() -> int:
  jobs = multiprocessing.cpu_count()
  return min(jobs, PROOT_MAX_JOBS) if "PRoot" in platform.release() else jobs

###  sudo chown -R $USER:$USER /opt/vcpkg

def generate_build_command(build_type: BuildType = BuildType.DEBUG, bench: bool = False, jobs: int = 0) -> str:
  benchmarks = "ON" if bench else "OFF"
  return (
    f"cd {BUILD_DIR_NAME} && cmake -DCMAKE_BUILD_TYPE={build_type.value} -DZ13_BUILD_BENCHMARKS={benchmarks} .. && "
    f"cmake --build . -j {jobs or default_jobs()} --config {build_type.value} && "
    f"cmake --install . --config {build_type.value} --prefix ../install")

def generate_build_command_old(build_type: BuildType = BuildType.DEBUG) -> str:
  return (
    f"cd {BUILD_DIR_NAME} && cmake -DCMAKE_BUILD_TYPE={build_type.value} .. && "
    f"cmake --build . -j {multiprocessing.cpu_count()} --config {build_type.value}")

def build(build_type: BuildType = BuildType.RELEASE, bench: bool = False, jobs: int = 0):
  path = Path(BUILD_DIR_NAME)
  path.mkdir(exist_ok=True)
  cmd = generate_build_command(build_type, bench, jobs)
  # print(f"command = {cmd}")
  os.system(cmd)

def run_tests(build_type: BuildType = BuildType.RELEASE):
  cmd = f"ctest --test-dir {BUILD_DIR_NAME} -C {build_type.value} --output-on-failure"
  os.system(cmd)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-b", "--build", action="store_true", help="debug build zodiac 13")
  parser.add_argument("-br", "--build-release", action="store_true", help="release build zodiac 13")
  parser.add_argument("-t", "--tests", action="store_true", help="run all tests")
  parser.add_argument("--bench", action="store_true",
                      help="also build the benchmark tests (tests/bench/); run them with z13.py --bench")
  parser.add_argument("-j", "--jobs", type=int, default=0,
                      help=f"parallel build jobs (default: all cores, at most {PROOT_MAX_JOBS} inside PRoot)")
  options = parser.parse_args()
  if options.tests:
    run_tests(BuildType.DEBUG if options.build else BuildType.RELEASE)
  elif options.build:
    build(BuildType.DEBUG, options.bench, options.jobs)
  else:
    build(BuildType.RELEASE, options.bench, options.jobs)

if __name__ == "__main__":
  main()