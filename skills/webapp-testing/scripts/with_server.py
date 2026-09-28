#!/usr/bin/env python3
"""Start servers, wait for their ports, run a command, then stop the servers.

  with_server.py --server "npm run dev" --port 5173 [--server CMD --port N ...] [--timeout 60] -- COMMAND...
"""
import argparse
import os
import signal
import socket
import subprocess
import sys
import time


def wait_port(port, timeout, proc):
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            return False
        with socket.socket() as s:
            s.settimeout(0.5)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return True
        time.sleep(0.25)
    return False


def main():
    if "--" not in sys.argv:
        sys.exit(__doc__)
    split = sys.argv.index("--")
    p = argparse.ArgumentParser()
    p.add_argument("--server", action="append", required=True)
    p.add_argument("--port", action="append", type=int, required=True)
    p.add_argument("--timeout", type=int, default=60)
    a = p.parse_args(sys.argv[1:split])
    command = sys.argv[split + 1:]
    if len(a.server) != len(a.port):
        sys.exit("give one --port per --server")
    procs = []
    try:
        for cmd, port in zip(a.server, a.port):
            proc = subprocess.Popen(cmd, shell=True, start_new_session=True)
            procs.append(proc)
            if not wait_port(port, a.timeout, proc):
                sys.exit(f"server did not open port {port}: {cmd}")
            print(f"ready: {cmd} on :{port}", file=sys.stderr)
        sys.exit(subprocess.call(command))
    finally:
        for proc in procs:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


if __name__ == "__main__":
    main()
