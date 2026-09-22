import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from srcds64_ui.model import Settings


@pytest.fixture
def fake_backend(tmp_path, monkeypatch):
    script = tmp_path / "fake backend.py"
    script.write_text("import sys\nprint('BACKEND ' + sys.argv[1], flush=True)\n"
                      "print('Destination: /home/steam/test server', flush=True)\n"
                      "sys.exit(3 if sys.argv[1] == 'verify' else 0)\n")
    original = Settings.command

    def command(settings, action, platform=None):
        return [sys.executable, str(script), *original(settings, action, "linux")[1:]]

    monkeypatch.setattr(Settings, "command", command)
    return script
