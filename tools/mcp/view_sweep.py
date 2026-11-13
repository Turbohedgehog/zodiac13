"""Measures the frame time of one spot looking in every direction, to see which views are heavy.

python3 tools/mcp/view_sweep.py --scene hub --x 46 --y 75 [--defines Z13_NO_SUN ...] [--unlimited]
Keep your hands off the device while it runs: a moved camera spoils the numbers.
"""

import argparse
import re
import time

from mcp_client import DEFAULT_PORT, ask

DEFAULT_YAWS = tuple(range(0, 360, 45))
DEFAULT_PITCHES = (-30, 0, 30)
SCENE_LOAD_SECONDS = 30
SETTLE_SECONDS = 2
DWELL_SECONDS = 4


def summary(report):
    def find(pattern):
        match = re.search(pattern, report)
        return float(match.group(1)) if match else float("nan")

    return {
        "fps": find(r"FPS: mean ([\d.]+)"),
        "ms": find(r"Frame: mean ([\d.]+) ms"),
        "p99": find(r"99th percentile ([\d.]+)"),
        "present": find(r"present \(swap, vsync\)\s+([\d.]+)"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--scene", help="starts a game in this scene first")
    parser.add_argument("--fragment-file", help="lighting.fs text to send instead of the shipped one")
    parser.add_argument("--x", type=float, required=True)
    parser.add_argument("--y", type=float, required=True)
    parser.add_argument("--z", type=float)
    parser.add_argument("--defines", nargs="*", default=[], help="lighting shader #defines for the run")
    parser.add_argument("--yaws", type=float, nargs="*", default=DEFAULT_YAWS, help="headings, degrees")
    parser.add_argument("--pitches", type=float, nargs="*", default=DEFAULT_PITCHES, help="positive looks down")
    parser.add_argument("--unlimited", action="store_true", help="takes off the frame limit")
    arguments = parser.parse_args()

    def call(command, payload=None):
        reply = ask(command, payload, arguments.port)
        if not reply.get("ok"):
            raise SystemExit(f"{command}: {reply.get('error')}")
        return reply["result"]

    if arguments.scene:
        call("start_game", {"scene": arguments.scene})
        time.sleep(SCENE_LOAD_SECONDS)
    if arguments.unlimited:
        call("frame_rate", {"unlimited": True})
    reload = {"defines": arguments.defines}
    if arguments.fragment_file:
        with open(arguments.fragment_file, encoding="utf-8") as source:
            reload["fragment"] = source.read()
    call("reload_shaders", reload)
    print(f"{'yaw':>5} {'pitch':>5} {'fps':>7} {'ms':>7} {'p99':>7} {'present':>8}")
    rows = []
    for pitch in arguments.pitches:
        for yaw in arguments.yaws:
            place = {"x": arguments.x, "y": arguments.y, "yaw_deg": yaw, "pitch_deg": pitch}
            if arguments.z is not None:
                place["z"] = arguments.z
            call("teleport", place)
            time.sleep(SETTLE_SECONDS)
            call("frame_report", {"restart": True})
            time.sleep(DWELL_SECONDS)
            row = summary(call("frame_report"))
            rows.append(row)
            print(f"{yaw:5.0f} {pitch:5.0f} {row['fps']:7.1f} {row['ms']:7.2f} {row['p99']:7.2f} {row['present']:8.2f}")
    mean_ms = sum(row["ms"] for row in rows) / len(rows)
    print(f"mean over the views: {mean_ms:.2f} ms, slowest {max(row['ms'] for row in rows):.2f} ms")
    call("reload_shaders")
    call("frame_rate")


if __name__ == "__main__":
    main()
