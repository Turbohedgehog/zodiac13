#!/usr/bin/env python3
"""Runs what make.py built: the game (default), a dedicated server, the tests, the benchmarks, or a
server with windowless clients playing input scripts over real sockets (--net-sim).

Arguments after `--` go to the game, the server or the test runner as they are, e.g.
`python3 z13.py --server 26214 -- --fps 30`.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from make import default_jobs

ROOT = Path(__file__).resolve().parent
# make.py's Debug and Release layouts: (build directory, binaries).
DEBUG_DIRS = (ROOT / "build", ROOT / "bin")
RELEASE_DIRS = (ROOT / "build-release", ROOT / "bin-release")
INSTALL_DIR = ROOT / "install"
GAME_NAME = "zodiac13"
TEST_RUNNER = Path("tests") / "z13_test_runner"
# Benchmarks are disabled gtests, built only by `make.py --bench`.
DEFAULT_BENCH_FILTER = "*.DISABLED_*"
# --net-sim: the station a desync showed on, the script each client plays, where the logs go.
NET_SIM_SCENE = "spire"
NET_SIM_SCRIPT = ROOT / "tools" / "input_scripts" / "walk.txt"
NET_SIM_LOGS = "net_sim"
NET_SIM_PORT = 26213
# environment.h: the data directory the game reads its settings from; --net-sim gives it a fresh one.
DATA_DIR_VARIABLE = "Z13_DATA_DIR"
NET_SIM_DATA = "data"
# divergence_dump.h: where both sides write their snapshots of a diverged tick.
DIVERGENCE_DIR_VARIABLE = "Z13_DIVERGENCE_DIR"
DIVERGENCE_FILES = "divergence_*.json"
# The server keeps its snapshots this long, so a client's report of a diverged tick (late on a
# loaded box, after the client writes its own JSON) still finds the server's. Its rollbacks stay
# bounded by max_late_ticks.
NET_SIM_SERVER_RETENTION = ["--snapshot-retention-seconds", 30]
NET_SIM_SERVER_READY = "NetSession(server): listening on port"
# What --net-sim counts in each process's log, and which of them fail the run.
NET_SIM_EVENTS = {
    "joined": ("NetSession(client): joined as player", False),
    "digests matched": ("state digest at tick", False),
    "resyncs": ("resyncing", True),
    "digest mismatches": ("state digest mismatch", True),
    "lost connections": ("the server closed the connection", True),
    "late commands moved": ("late or early commands", False),
    "errors": ("[error]", True),
}


def executable(directory: Path, name: str) -> Path:
  path = directory / (name + (".exe" if sys.platform == "win32" else ""))
  if not path.exists():
    sys.exit(f"{path} not found: build it first with make.py")
  return path


def run(command: list, cwd: Path) -> int:
  print(" ".join(str(part) for part in command), flush=True)
  return subprocess.call([str(part) for part in command], cwd=cwd)


def wait_for_line(log: Path, text: str, process: subprocess.Popen, timeout: float) -> bool:
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if log.exists() and text in log.read_text(errors="replace"):
      return True
    if process.poll() is not None:
      return False
    time.sleep(0.5)
  return False


def start(command: list, log: Path, cwd: Path, data: Path) -> subprocess.Popen:
  print(" ".join(str(part) for part in command), ">", log, flush=True)
  env = {**os.environ, DATA_DIR_VARIABLE: str(data), DIVERGENCE_DIR_VARIABLE: str(log.parent)}
  return subprocess.Popen([str(part) for part in command], cwd=cwd, env=env, stdout=log.open("w"),
                          stderr=subprocess.STDOUT)


def stop(process: subprocess.Popen) -> int:
  if process.poll() is None:
    process.terminate()  # SIGTERM: Core::Run shuts the worlds down cleanly
    try:
      return process.wait(timeout=30)
    except subprocess.TimeoutExpired:
      process.kill()
  return process.wait()


def summarize(logs: list) -> bool:
  failed = False
  for log, code in logs:
    text = log.read_text(errors="replace")
    counts = {name: text.count(pattern) for name, (pattern, _) in NET_SIM_EVENTS.items()}
    bad = code != 0 or any(counts[name] for name, (_, fails) in NET_SIM_EVENTS.items() if fails)
    failed |= bad
    listed = ", ".join(f"{name} {count}" for name, count in counts.items() if count)
    print(f"{'FAIL' if bad else 'ok  '} {log.name}: exit {code}" + (f", {listed}" if listed else ""))
  return not failed


def net_sim(game: Path, game_dir: Path, options: argparse.Namespace, extra: list) -> int:
  logs = (options.log_dir or game_dir / NET_SIM_LOGS).resolve()
  logs.mkdir(parents=True, exist_ok=True)
  data = logs / NET_SIM_DATA
  shutil.rmtree(data, ignore_errors=True)
  for old in logs.glob(DIVERGENCE_FILES):
    old.unlink()
  script = Path(options.script).resolve()
  server_log = logs / "server.log"
  scene = ["--station-scene", options.scene] if options.scene else []
  server = start([game, "--server", options.port, *scene, *NET_SIM_SERVER_RETENTION, *extra], server_log, game_dir, data / "server")
  if not wait_for_line(server_log, NET_SIM_SERVER_READY, server, options.timeout):
    stop(server)
    print(f"the server didn't start: see {server_log}")
    return 1

  clients = []
  for index in range(options.clients):
    log = logs / f"client_{index + 1}.log"
    command = [game, "--headless", "--connect", f"127.0.0.1:{options.port}", "--input-script", script, *extra]
    clients.append((start(command, log, game_dir, data / f"client_{index + 1}"), log))
    time.sleep(options.stagger)

  deadline = time.monotonic() + options.timeout
  results = []
  for process, log in clients:
    try:
      code = process.wait(timeout=max(0.0, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
      print(f"{log.name} still running after {options.timeout} s, stopping it")
      code = stop(process)
    results.append((log, code))
  results.insert(0, (server_log, stop(server)))
  passed = summarize(results)
  for dump in sorted(logs.glob(DIVERGENCE_FILES)):
    print(f"diverged state: {dump}")
  return 0 if passed else 1


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  mode = parser.add_mutually_exclusive_group()
  mode.add_argument("--game", action="store_true", help="run the game (default)")
  mode.add_argument("--server", nargs="?", const=0, type=int, metavar="PORT",
                    help="run a dedicated server, on PORT or the default port")
  mode.add_argument("--tests", action="store_true", help="run the tests (ctest, or the runner with --filter)")
  mode.add_argument("--bench", action="store_true", help="run the benchmarks (needs make.py --bench)")
  mode.add_argument("--net-sim", action="store_true",
                    help="run a server and --clients windowless clients playing --script, then sum up their logs")
  parser.add_argument("--clients", type=int, default=2, help="--net-sim: how many clients")
  parser.add_argument("--scene", default=NET_SIM_SCENE, help="--net-sim: the server's station (empty: none)")
  parser.add_argument("--script", default=NET_SIM_SCRIPT, help="--net-sim: the input script every client plays")
  parser.add_argument("--port", type=int, default=NET_SIM_PORT, help="--net-sim: the server's port")
  parser.add_argument("--stagger", type=float, default=2.0, help="--net-sim: seconds between clients joining")
  parser.add_argument("--timeout", type=float, default=600.0,
                      help="--net-sim: seconds to wait for the server to start and for the clients to finish")
  parser.add_argument("--log-dir", type=Path, help="--net-sim: where the logs go (default: bin/net_sim)")
  parser.add_argument("--filter", help="gtest filter for --tests and --bench")
  parser.add_argument("--from", dest="source", choices=["build", "install"], default="build",
                      help="run the game or server from bin/ (build, default) or install/")
  parser.add_argument("--release", action="store_true",
                      help="run what make.py -br built (build-release/, bin-release/) instead of Debug")
  parser.add_argument("extra", nargs=argparse.REMAINDER, help="arguments after -- are passed through")
  options = parser.parse_args()
  extra = options.extra[1:] if options.extra[:1] == ["--"] else options.extra
  build_dir, bin_dir = RELEASE_DIRS if options.release else DEBUG_DIRS
  test_runner = bin_dir / TEST_RUNNER

  if options.tests:
    if options.filter:
      return run([executable(test_runner.parent, test_runner.name), f"--gtest_filter={options.filter}", *extra],
                 test_runner.parent)
    return run(["ctest", "--test-dir", build_dir, "--output-on-failure", "--parallel", str(default_jobs()), *extra], ROOT)
  if options.bench:
    return run([executable(test_runner.parent, test_runner.name), "--gtest_also_run_disabled_tests",
                f"--gtest_filter={options.filter or DEFAULT_BENCH_FILTER}", *extra], test_runner.parent)

  game_dir = INSTALL_DIR if options.source == "install" else bin_dir
  game = executable(game_dir, GAME_NAME)
  if options.server is not None:
    server = ["--server"] + ([str(options.server)] if options.server else [])
    return run([game, *server, *extra], game_dir)
  if options.net_sim:
    return net_sim(game, game_dir, options, extra)
  return run([game, *extra], game_dir)


if __name__ == "__main__":
  sys.exit(main())
