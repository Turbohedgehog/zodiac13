"""A client of the game's debug channel (docs/debug-server-plan.md): one JSON request per line over loopback."""

import json
import socket

DEFAULT_PORT = 28123
TIMEOUT_SECONDS = 20


def ask(command, arguments=None, port=DEFAULT_PORT):
    """Sends one command and returns its reply: {"id", "ok", "result" or "error"}."""
    request = {"id": 1, "command": command, "arguments": arguments or {}}
    with socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT_SECONDS) as connection:
        connection.sendall((json.dumps(request) + "\n").encode())
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = connection.recv(65536)
            if not chunk:
                break
            reply += chunk
    return json.loads(reply)
