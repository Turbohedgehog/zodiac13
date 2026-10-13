#!/usr/bin/env python3
"""Finds files the branch changed whose base version used CRLF line endings while the
working copy no longer does, and restores CRLF (a text-mode rewrite drops it).

Usage: crlf_restore.py [--check] [BASE]   (BASE defaults to main)
"""

import subprocess
import sys


def main() -> int:
    args = [arg for arg in sys.argv[1:] if arg != "--check"]
    check = "--check" in sys.argv[1:]
    base = args[0] if args else "main"
    changed = subprocess.run(["git", "diff", "--name-only", base], capture_output=True, text=True,
                             check=True).stdout.split()
    found = 0
    for path in changed:
        original = subprocess.run(["git", "show", f"{base}:{path}"], capture_output=True).stdout
        if b"\r\n" not in original:
            continue
        try:
            current = open(path, "rb").read()
        except FileNotFoundError:
            continue
        if current.count(b"\n") == current.count(b"\r\n"):
            continue
        found += 1
        print(path)
        if not check:
            open(path, "wb").write(current.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
    return 1 if check and found else 0


if __name__ == "__main__":
    sys.exit(main())
