#!/usr/bin/env python3
"""Stops the build and tests build.py started, with every compiler process under them, by the
PID it recorded: no matching of command lines (`pkill -f` on a pattern from the caller's own
command line kills the calling shell, CLAUDE.md "Build & test").

Usage: stop_build.py [--log-dir DIR]
"""

import argparse
import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_LOG_DIR = Path(tempfile.gettempdir()) / "z13-build"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    args = parser.parse_args()
    pid_file = args.log_dir / "build.pid"
    if not pid_file.exists():
        print("no build started by build.py in this log directory")
        return 0
    pid = int(pid_file.read_text())
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], check=True)
        else:
            # build.py starts the runner in a session of its own, so its group is the whole build.
            os.killpg(pid, signal.SIGTERM)
    except (ProcessLookupError, subprocess.CalledProcessError):
        print("already finished")
    else:
        print(f"stopped build {pid}")
    pid_file.unlink()
    return 0


if __name__ == "__main__":
    sys.exit(main())
