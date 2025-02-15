#!/usr/bin/env python3

import os
from pathlib import Path
from enum import Enum
import multiprocessing

class BuildType(Enum):
  RELEASE = "Release"
  DEBUG = "Debug"

BUILD_DIR_NAME = "build"

def generate_build_command(build_type: BuildType = BuildType.DEBUG) -> str:
  return (
    f"cd {BUILD_DIR_NAME} && cmake -DCMAKE_BUILD_TYPE={build_type.value} .. && "
    f"cmake --build . -j {multiprocessing.cpu_count()} --config {build_type.value} && "
    f"cmake --install . --config {build_type.value} --prefix ../install")

def main():
  path = Path(BUILD_DIR_NAME)
  path.mkdir(exist_ok=True)
  cmd = generate_build_command()
  print(f"command = {cmd}")
  os.system(cmd)

if __name__ == "__main__":
  main()