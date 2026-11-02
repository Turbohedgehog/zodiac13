#!/usr/bin/env python3
"""Fails on a plugin's source that includes a header of another plugin.

Two plugins share code through a component header (z13/components) or a stateless
library, never through each other's include directories: a plugin is loaded and
reloaded on its own. Tests link the plugins' factories and are not checked.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULES = ROOT / "src" / "lib_z13" / "modules"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([a-z0-9_]+_module)/', re.M)
SOURCE_SUFFIXES = (".cpp", ".h")


def main():
    plugins = {path.name for path in MODULES.iterdir() if path.is_dir()}
    tracked = subprocess.check_output(["git", "ls-files", str(MODULES)], cwd=ROOT, text=True).split()
    failures = []
    for name in tracked:
        path = ROOT / name
        parts = path.relative_to(MODULES).parts
        if path.suffix not in SOURCE_SUFFIXES or "tests" in parts:
            continue
        for plugin in INCLUDE.findall(path.read_text(errors="replace")):
            if plugin in plugins and plugin != parts[0]:
                failures.append(f"{path.relative_to(ROOT)} includes {plugin}")
    if failures:
        print("A plugin includes another plugin's header:")
        print("\n".join(sorted(failures)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
