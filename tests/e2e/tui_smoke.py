#!/usr/bin/env python3
"""Drive the full-screen TUI in a pseudo-terminal: type a message, wait for
the reply, open the command palette, quit. Usage: tui_smoke.py <shaman>"""
import os
import pty
import select
import sys
import time

shaman = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execv(shaman, [shaman])

out = b""


def wait_for(needle, timeout=15):
    global out
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
        if needle.encode() in out:
            return True
    return False


def send(data):
    os.write(fd, data.encode() if isinstance(data, str) else data)


ok = wait_for("shaman")
send("hello from tui\r")
ok = ok and wait_for("echo: hello from tui")
send("\x10")  # ctrl-p: palette
ok = ok and wait_for("Commands")
send("\x1b")  # close palette
time.sleep(0.3)
send("/cost\r")
ok = ok and wait_for("output tokens")
send("\x03")  # ctrl-c on empty input quits
time.sleep(0.5)
try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
print("tui ok" if ok else "tui FAILED\n" + out.decode(errors="replace")[-2000:])
sys.exit(0 if ok else 1)
