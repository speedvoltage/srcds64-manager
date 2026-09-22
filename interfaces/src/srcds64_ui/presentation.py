"""Shared labels and plain-text rendering of untrusted process output."""
import re
import sys

from .model import GAMES, Settings

FIELDS = (
    ("backend", "Installer executable", "srcds64 or /absolute/path/to/srcds64"),
    ("install_dir", "Server directory", "Blank: servers/<game>-server in your home folder"),
    ("data_dir", "Tool and cache directory", "Blank: backend default"),
    ("steamcmd", "SteamCMD executable", "Blank: discover or download"),
    ("depot_downloader", "DepotDownloader executable", "Blank: discover or download"),
)
if sys.platform == "win32":
    FIELDS = tuple(field for field in FIELDS if field[0] != "backend")

CONFIRM = ("Install a fresh native 64-bit server?\n\n"
           "This downloads Steam tools, game files and runtime files. "
           "The backend refuses a non-empty server directory. "
           "Use Preview first to review the destination and steps.\n\n"
           "Keep this interface open until the operation finishes.")


def plain_output(text: str) -> str:
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    return "".join(c for c in text.replace("\r", "\n") if c in "\n\t" or c.isprintable())


def confirmation_text(settings: Settings) -> str:
    game = next(g["name"] for g in GAMES if g["id"] == settings.game)
    destination = settings.install_dir or f"~/servers/{settings.game}-server (your home folder)"
    return f"{game}\nServer directory: {destination}\n\n{CONFIRM}"
