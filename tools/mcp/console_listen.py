"""Prints what the player says in the game console's Assistant tab, one line per message.

Run it under Monitor, so each line reaches the assistant's session as a notification; the answer goes back with
the `console_say` command (docs/console-plan.md). It subscribes to the debug channel and reconnects when the game
restarts.

    python3 tools/mcp/console_listen.py [--port 28123]
"""

import argparse
import json
import socket
import sys
import time

DEFAULT_PORT = 28123
RECONNECT_SECONDS = 3
SUBSCRIBE = {"id": 1, "command": "subscribe", "arguments": {}}


def listen(port):
    """Yields the text of each `say` event until the connection ends."""
    with socket.create_connection(("127.0.0.1", port), timeout=RECONNECT_SECONDS) as connection:
        connection.settimeout(None)
        connection.sendall((json.dumps(SUBSCRIBE) + "\n").encode())
        for line in connection.makefile("r", encoding="utf-8"):
            message = json.loads(line)
            if message.get("event") == "say":
                yield message["text"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    port = parser.parse_args().port
    while True:
        try:
            for text in listen(port):
                print(text, flush=True)
        except (OSError, json.JSONDecodeError) as error:
            print(f"(no game on port {port}: {error}; trying again)", file=sys.stderr, flush=True)
        time.sleep(RECONNECT_SECONDS)


if __name__ == "__main__":
    main()
