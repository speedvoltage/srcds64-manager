"""One background process per interface, with bounded live output.

stdin is closed so the original CLI uses the current Linux account and its
noninteractive defaults. No shell, sudo, or command string interpolation.
"""
from __future__ import annotations

import codecs
import os
import sys
from dataclasses import dataclass
from queue import Empty, Queue
import subprocess
import threading


@dataclass(frozen=True)
class Event:
    kind: str
    text: str = ""
    code: int | None = None


class Job:
    def __init__(self, command: list[str]):
        self.command = list(command)
        self.events: Queue[Event] = Queue(maxsize=256)
        self.thread = threading.Thread(target=self._run, name="srcds64-backend", daemon=False)

    def start(self) -> None:
        self.thread.start()

    def drain(self, limit: int = 128) -> list[Event]:
        result = []
        for _ in range(limit):
            try:
                result.append(self.events.get_nowait())
            except Empty:
                break
        return result

    def _run(self) -> None:
        try:
            options = {"creationflags": subprocess.CREATE_NO_WINDOW} if sys.platform == "win32" else {}
            environment = {**os.environ, "PYTHONIOENCODING": "utf-8"}
            with subprocess.Popen(self.command, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  env=environment, bufsize=0, **options) as process:
                decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
                assert process.stdout is not None
                while chunk := process.stdout.read(4096):
                    self.events.put(Event("output", decoder.decode(chunk)))
                tail = decoder.decode(b"", final=True)
                if tail:
                    self.events.put(Event("output", tail))
                code = process.wait()
            self.events.put(Event("finished", code=code))
        except OSError as error:
            self.events.put(Event("output", f"Unable to run installer: {error}\n"
                                  "On Linux, build srcds64 and set its absolute path in Settings. "
                                  "On Windows, extract the complete portable package before launching.\n"))
            self.events.put(Event("finished", code=127))
