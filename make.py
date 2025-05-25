#!/usr/bin/env python3

import os
import argparse
from pathlib import Path
from enum import Enum
import multiprocessing

class BuildType(Enum):
  RELEASE = "Release"
  DEBUG = "Debug"

  def __str__(self):
    return self.value

BUILD_DIR_NAME = "build"

###  sudo chown -R $USER:$USER /opt/vcpkg

def generate_build_command(build_type: BuildType = BuildType.DEBUG) -> str:
  return (
    f"cd {BUILD_DIR_NAME} && cmake -DCMAKE_BUILD_TYPE={build_type.value} .. && "
    f"cmake --build . -j {multiprocessing.cpu_count()} --config {build_type.value} && "
    f"cmake --install . --config {build_type.value} --prefix ../install")

def generate_build_command_old(build_type: BuildType = BuildType.DEBUG) -> str:
  return (
    f"cd {BUILD_DIR_NAME} && cmake -DCMAKE_BUILD_TYPE={build_type.value} .. && "
    f"cmake --build . -j {multiprocessing.cpu_count()} --config {build_type.value}")

def build(build_type: BuildType = BuildType.RELEASE):
  path = Path(BUILD_DIR_NAME)
  path.mkdir(exist_ok=True)
  cmd = generate_build_command(build_type)
  # print(f"command = {cmd}")
  os.system(cmd)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-b", "--build", action="store_true", help="debug build zodiac 13")
  parser.add_argument("-br", "--build-release", action="store_true", help="release build zodiac 13")
  options = parser.parse_args()
  has_some_action = False
  if options.build:
    build(BuildType.DEBUG)
    has_some_action = True
  if options.build_release:
    build(BuildType.RELEASE)
    has_some_action = True

  if not has_some_action:
    build(BuildType.RELEASE)

if __name__ == "__main__":
  main()