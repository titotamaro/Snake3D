#!/usr/bin/env python3
"""Command-line client for the Ouroboros editor's agent automation API (JSON lines over TCP, 127.0.0.1:7420).

Examples:
  python Tools/obctl.py ping
  python Tools/obctl.py help
  python Tools/obctl.py entity.list
  python Tools/obctl.py component.set '{"entity": "Sun", "type": "DirectionalLight", "data": {"Intensity": 5}}'
  python Tools/obctl.py editor.screenshot '{"path": "shot.png", "ui": true}'
  python Tools/obctl.py --script OuroborosTests/Data/EditorAutomation.jsonl

Exit code: 0 if every command succeeded, 1 otherwise. Responses are printed as JSON (one per line).
"""

import argparse
import json
import socket
import sys


def send_requests(host, port, requests, timeout):
    with socket.create_connection((host, port), timeout=timeout) as connection:
        stream = connection.makefile("rwb")
        responses = []
        for request in requests:
            stream.write((json.dumps(request) + "\n").encode("utf-8"))
            stream.flush()
            line = stream.readline()
            if not line:
                raise ConnectionError("the editor closed the connection")
            responses.append(json.loads(line))
        return responses


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", help="command name, e.g. entity.list")
    parser.add_argument("args", nargs="?", default="{}", help="JSON object with the command arguments")
    parser.add_argument("--script", help="file with one JSON request per line (# comments allowed)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7420)
    parser.add_argument("--timeout", type=float, default=120.0, help="seconds to wait for each response")
    parser.add_argument("--pretty", action="store_true", help="indent the output")
    arguments = parser.parse_args()

    requests = []
    if arguments.script:
        with open(arguments.script, encoding="utf-8") as file:
            for line in file:
                line = line.strip()
                if line and not line.startswith("#"):
                    requests.append(json.loads(line))
    elif arguments.command:
        try:
            args = json.loads(arguments.args)
        except json.JSONDecodeError as error:
            parser.error("args must be a JSON object: %s" % error)
        requests.append({"id": 1, "cmd": arguments.command, "args": args})
    else:
        parser.error("give a command or --script")

    try:
        responses = send_requests(arguments.host, arguments.port, requests, arguments.timeout)
    except OSError as error:
        print("could not reach the editor at %s:%d (%s). Is OuroborosEditor running?" % (arguments.host, arguments.port, error), file=sys.stderr)
        return 1

    failed = False
    for response in responses:
        print(json.dumps(response, indent=2 if arguments.pretty else None))
        failed = failed or not response.get("ok", False)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
