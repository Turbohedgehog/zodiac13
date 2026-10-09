#!/usr/bin/env python3
"""Fails on a tracked .cpp or .fbs under src/ that no CMakeLists.txt lists.

Sources are listed explicitly rather than globbed (a glob misses a new file until the
next configure), so a forgotten entry would otherwise drop a file, or a whole test,
without an error.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = (".cpp", ".fbs")
SET_ONE_VALUE = re.compile(r"^\s*set\(\s*(\w+)\s+(\S+)\s*\)", re.M)
VARIABLE = re.compile(r"\$\{(\w+)\}")
LISTED_PATH = re.compile(r"[^\s()\"]+(?:\.cpp|\.fbs)\b")


def listed_sources(cmake_file):
    """Paths, relative to ROOT, of the sources a CMakeLists.txt names."""
    directory = cmake_file.parent
    text = cmake_file.read_text()
    variables = {
        "CMAKE_SOURCE_DIR": str(ROOT),
        "PROJECT_SOURCE_DIR": str(directory),
        "CMAKE_CURRENT_SOURCE_DIR": str(directory),
    }
    for name, value in SET_ONE_VALUE.findall(text):
        variables.setdefault(name, VARIABLE.sub(lambda m: variables.get(m.group(1), m.group(0)), value))
    listed = set()
    for token in LISTED_PATH.findall(text):
        path = pathlib.Path(VARIABLE.sub(lambda m: variables.get(m.group(1), m.group(0)), token))
        listed.add((directory / path).resolve())
    return listed


def main():
    tracked = subprocess.run(["git", "ls-files", "src"], cwd=ROOT, capture_output=True, text=True,
                             check=True).stdout.split()
    sources = {(ROOT / p).resolve() for p in tracked if p.endswith(SOURCE_SUFFIXES)}
    listed = set().union(*(listed_sources(ROOT / p) for p in
                           subprocess.run(["git", "ls-files", "*CMakeLists.txt"], cwd=ROOT,
                                          capture_output=True, text=True, check=True).stdout.split()))
    unlisted = sorted(str(p.relative_to(ROOT)) for p in sources - listed)
    for path in unlisted:
        print(f"{path}: not listed in any CMakeLists.txt", file=sys.stderr)
    return 1 if unlisted else 0


if __name__ == "__main__":
    sys.exit(main())
