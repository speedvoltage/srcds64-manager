"""Shared settings and shell-free command construction.

Windows uses the bundled native installer; Linux uses the original C++ CLI.
Paths always refer to the host operating system.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, fields
from importlib.resources import files
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import sys
import tempfile

GAMES = json.loads(files("srcds64_ui").joinpath("games.json").read_text(encoding="utf-8"))
ACTIONS = ("plan", "doctor", "install", "verify")
NOTICE = "Native Windows x64 server installer" if sys.platform == "win32" else "Linux x64 server installer"


@dataclass(frozen=True)
class Settings:
    game: str = "hl2dm"
    backend: str = "srcds64"
    install_dir: str = ""
    data_dir: str = ""
    steamcmd: str = ""
    depot_downloader: str = ""
    validate: bool = True

    def command(self, action: str, platform: str | None = None) -> list[str]:
        platform = platform or sys.platform
        if platform not in ("win32", "linux"):
            raise ValueError("This interface supports native Windows and Linux.")
        if action not in ACTIONS:
            raise ValueError(f"Unknown action: {action}")
        if self.game not in {game["id"] for game in GAMES}:
            raise ValueError("Choose a supported game.")
        for field in fields(self):
            value = getattr(self, field.name)
            if isinstance(value, str) and any(c in value for c in ("\0", "\n", "\r")):
                raise ValueError(f"{field.name}: control characters are not allowed.")
        if platform == "linux":
            if not self.backend.strip() or self.backend.startswith("-"):
                raise ValueError("Set the Linux installer executable (srcds64 or an absolute path).")
            if "/" in self.backend and not PurePosixPath(self.backend).is_absolute():
                raise ValueError("Use an absolute Linux path for the installer executable.")
            if "\\" in self.backend or ":" in self.backend:
                raise ValueError("Use a Linux installer executable.")
        path_type = PureWindowsPath if platform == "win32" else PurePosixPath
        for name in ("install_dir", "data_dir", "steamcmd", "depot_downloader"):
            value = getattr(self, name)
            if value and not path_type(value).is_absolute():
                label = "Windows" if platform == "win32" else "Linux"
                raise ValueError(f"{name}: use an absolute {label} path, or leave blank for the default.")
        if platform == "win32":
            executable = Path(sys.executable)
            # pythonw has no usable stdout; run the worker with python.exe and hide its window.
            if executable.name.casefold() == "pythonw.exe":
                executable = executable.with_name("python.exe")
            prefix = [str(executable), "-u", "-m", "srcds64_ui.windows_backend"]
        else:
            prefix = [self.backend]
        args = [*prefix, action]
        if action != "doctor":
            args += ["--game", self.game]
            if self.install_dir:
                args += ["--install-dir", self.install_dir]
        for name, option in (("data_dir", "--data-dir"), ("steamcmd", "--steamcmd"),
                             ("depot_downloader", "--depot-downloader")):
            if value := getattr(self, name):
                args += [option, value]
        if action == "install":
            args.append("--yes")
            if not self.validate:
                args.append("--no-validate")
        if action == "plan" and not self.validate:
            args.append("--no-validate")
        return args


def settings_path() -> Path:
    if sys.platform == "win32":
        root = Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming"))
    else:
        root = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return root / "srcds64-manager" / "interfaces.json"


def load_settings(path: Path | None = None) -> Settings:
    path = path or settings_path()
    if not path.exists():
        return Settings()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("Expected a settings object.")
        values = {f.name: data[f.name] for f in fields(Settings) if f.name in data}
        for key, value in values.items():
            expected = bool if key == "validate" else str
            if type(value) is not expected:
                raise ValueError(f"Invalid value for {key}.")
        return Settings(**values)
    except (OSError, ValueError, TypeError) as error:
        raise ValueError(f"Cannot read settings at {path}: {error}") from error


def save_settings(settings: Settings, path: Path | None = None) -> None:
    path = path or settings_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(asdict(settings), stream, indent=2)
            stream.write("\n")
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
