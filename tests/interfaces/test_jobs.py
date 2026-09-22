import sys
import time

from srcds64_ui.jobs import Job
from srcds64_ui.presentation import plain_output


def collect(job):
    job.start()
    events = []
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        events.extend(job.drain())
        if any(event.kind == "finished" for event in events):
            job.thread.join(timeout=1)
            assert not job.thread.is_alive()
            return events
        time.sleep(0.01)
    raise AssertionError("Backend process failed to complete")


def test_process_streams_stdout_stderr_and_exit_status():
    script = "import sys; print('ready', flush=True); print('failure', file=sys.stderr); sys.exit(7)"
    events = collect(Job([sys.executable, "-c", script]))
    output = "".join(event.text for event in events)
    assert "ready" in output and "failure" in output
    assert events[-1].code == 7


def test_stdin_is_closed_and_utf8_chunks_decode():
    script = ("import os, sys, time; assert sys.stdin.read() == ''; "
              "os.write(1, b'\\xe2'); time.sleep(.05); os.write(1, b'\\x9c\\x93')")
    events = collect(Job([sys.executable, "-c", script]))
    assert "".join(event.text for event in events) == "✓"
    assert events[-1].code == 0


def test_missing_backend_is_an_actionable_failure(tmp_path):
    events = collect(Job([str(tmp_path / "missing")]))
    assert events[-1].code == 127
    assert "build srcds64" in events[0].text


def test_log_is_plain_text():
    assert plain_output("\x1b[31m[FAIL]\x1b[0m\x00\rtest") == "[FAIL]\ntest"
