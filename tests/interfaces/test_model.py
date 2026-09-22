from dataclasses import replace
import json
import sys
from pathlib import Path

import pytest

from srcds64_ui.model import GAMES, Settings, load_settings, save_settings


def test_packaged_catalog_matches_backend():
    source = Path(__file__).parents[2] / "resources/games.json"
    assert GAMES == json.loads(source.read_text())


@pytest.mark.parametrize("platform", ["linux", "win32"])
@pytest.mark.parametrize("action", ["plan", "doctor", "install", "verify"])
def test_commands_are_noninteractive_and_preserve_arguments(platform, action):
    path = r"C:\Servers\my server & test" if platform == "win32" else "/home/steam/my server; $(touch nope)"
    data = r"C:\Users\steam\data" if platform == "win32" else "/home/steam/data"
    settings = Settings(install_dir=path, backend="/opt/my installer/srcds64",
                        validate=False, data_dir=data)
    command = settings.command(action, platform)
    if platform == "win32":
        assert command[:5] == [sys.executable, "-u", "-m", "srcds64_ui.windows_backend", action]
        assert "wsl.exe" not in command
    else:
        assert command[:2] == [settings.backend, action]
    assert ("--yes" in command) == (action == "install")
    assert ("--no-validate" in command) == (action in ("plan", "install"))
    assert (path in command) == (action != "doctor")
    assert "--owner" not in command


def test_windows_gui_uses_console_python_worker(monkeypatch, tmp_path):
    monkeypatch.setattr(sys, "executable", str(tmp_path / "runtime/pythonw.exe"))
    assert Settings().command("plan", "win32")[:2] == [str(tmp_path / "runtime/python.exe"), "-u"]


@pytest.mark.parametrize("changes", [
    {"game": "unknown"}, {"backend": ""}, {"backend": "./build/srcds64"},
    {"backend": "C:\\srcds64.exe"}, {"backend": "--help"},
    {"install_dir": "~/server"}, {"install_dir": "C:\\servers"},
    {"data_dir": "relative"}, {"steamcmd": "bad\npath"},
])
def test_invalid_linux_settings_rejected(changes):
    with pytest.raises(ValueError):
        replace(Settings(), **changes).command("install", "linux")


@pytest.mark.parametrize("path", ["/home/steam/server", "C:server", "server", "~/server", "C:\\server\n"])
def test_windows_rejects_non_native_or_relative_paths(path):
    with pytest.raises(ValueError):
        Settings(install_dir=path).command("install", "win32")


def test_legacy_wsl_settings_do_not_run_wsl(tmp_path):
    path = tmp_path / "settings.json"
    path.write_text('{"distribution": "Ubuntu", "backend": "/usr/local/bin/srcds64"}')
    command = load_settings(path).command("plan", "win32")
    assert "wsl.exe" not in command
    assert "/usr/local/bin/srcds64" not in command


def test_settings_roundtrip_and_atomic_replacement(tmp_path):
    path = tmp_path / "config/settings.json"
    settings = Settings(game="css", install_dir="/home/steam/server ü", validate=False)
    save_settings(settings, path)
    assert load_settings(path) == settings
    save_settings(Settings(), path)
    assert load_settings(path) == Settings()
    assert list(path.parent.iterdir()) == [path]


@pytest.mark.parametrize("contents", ['[]', '{', '{"validate":"false"}', '{"game":2}'])
def test_malformed_settings_reported(tmp_path, contents):
    path = tmp_path / "settings.json"
    path.write_text(contents)
    with pytest.raises(ValueError, match="Cannot read settings"):
        load_settings(path)


def test_missing_settings_defaults_and_unknown_keys_ignored(tmp_path):
    path = tmp_path / "settings.json"
    assert load_settings(path) == Settings()
    path.write_text('{"future_option": 42, "game": "css"}')
    assert load_settings(path).game == "css"
