#!/usr/bin/env python3
"""Prints how many object files depend on each header, from `ninja -t deps` in build/: the
number of objects an edit to that header rebuilds.

Usage: header_width.py [HEADER_SUFFIX...]   (no arguments: the 40 widest headers)
"""

import collections
import subprocess
import sys
from pathlib import Path

TOP = 40


def main() -> int:
    if not Path("build/build.ninja").exists():
        print("needs a Ninja build in build/: other generators (Visual Studio) keep no dependency log")
        return 1
    deps = subprocess.run(["ninja", "-C", "build", "-t", "deps"], capture_output=True, text=True,
                          check=True).stdout
    dependents = collections.defaultdict(set)
    current = None
    for line in deps.splitlines():
        if line and not line.startswith(" "):
            current = line.split(":")[0]
        elif line.strip():
            dependents[line.strip()].add(current)
    objects = {obj for objs in dependents.values() for obj in objs}
    print(f"{len(objects)} objects")
    headers = {path: len(objs) for path, objs in dependents.items() if path.endswith(".h")}
    if len(sys.argv) > 1:
        for suffix in sys.argv[1:]:
            matches = [path for path in headers if path.endswith(suffix)]
            print(f"{headers[matches[0]] if matches else 0:4} {suffix}")
    else:
        for path, count in sorted(headers.items(), key=lambda item: -item[1])[:TOP]:
            print(f"{count:4} {path.split('/zodiac13/')[-1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
