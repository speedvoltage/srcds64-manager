"""Native Windows SteamCMD installer; no WSL, shell commands or elevation.

Linux retains the upstream C++ backend. This backend is bundled into the Windows
interfaces and can also run as `python -m srcds64_ui.windows_backend`.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from typing import Callable
import urllib.request
import uuid
import zipfile

from .model import GAMES

STEAMCMD_URL = "https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip"
DEPOT_URL = "https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_3.4.0/DepotDownloader-windows-x64.zip"
DEPOT_SHA256 = "41c9e9f0df54b3ad02e67a11726756e5c73283bd7c2e1b04acfa5ae4c2ed3767"
DONOR_APP = "232250"
DONOR_DEPOT = "232255"
RUNTIME_DLLS = ("dedicated.dll", "engine.dll", "filesystem_stdio.dll", "tier0.dll",
                "vstdlib.dll", "steam_api64.dll", "steamclient64.dll", "tier0_s64.dll", "vstdlib_s64.dll")


class InstallError(RuntimeError):
    pass


class ToolExitError(InstallError):
    def __init__(self, command: str, code: int):
        super().__init__(f"Tool exited with code {code}: {command}")
        self.code = code


def is_pe(path: Path, machine: int = 0x8664) -> bool:
    """Check DOS/PE signatures, architecture and matching optional-header format."""
    try:
        with path.open("rb") as stream:
            dos = stream.read(64)
            if len(dos) != 64 or dos[:2] != b"MZ":
                return False
            offset = struct.unpack_from("<I", dos, 60)[0]
            if offset < 64 or offset > path.stat().st_size - 26:
                return False
            stream.seek(offset)
            header = stream.read(26)
            optional_size = struct.unpack_from("<H", header, 20)[0]
            return (header[:4] == b"PE\0\0"
                    and struct.unpack_from("<H", header, 4)[0] == machine
                    and optional_size >= (240 if machine == 0x8664 else 224)
                    and path.stat().st_size >= offset + 24 + optional_size
                    and struct.unpack_from("<H", header, 24)[0] == (0x20B if machine == 0x8664 else 0x10B))
    except (OSError, struct.error):
        return False


def extract_zip(archive: Path, destination: Path) -> None:
    """Reject archive paths that are unsafe on Windows, even in Linux tests."""
    with zipfile.ZipFile(archive) as zipped:
        if len(zipped.infolist()) > 5000 or sum(i.file_size for i in zipped.infolist()) > 512 * 1024 * 1024:
            raise InstallError("Tool archive exceeds extraction limits.")
        seen = set()
        for item in zipped.infolist():
            name = item.filename.replace("\\", "/")
            path = PurePosixPath(name)
            reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}
            if (path.is_absolute() or not path.parts or ".." in path.parts
                    or any(":" in part or part.endswith((".", " ")) or part.split(".")[0].upper() in reserved for part in path.parts)
                    or ((item.external_attr >> 16) & 0o170000) == 0o120000
                    or name.casefold() in seen):
                raise InstallError(f"Unsafe tool archive entry: {name}")
            seen.add(name.casefold())
        zipped.extractall(destination)


def download(url: str, destination: Path, sha256: str | None = None) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "srcds64-manager/0.2"})
    digest = hashlib.sha256()
    size = 0
    with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
        if not response.geturl().startswith("https://"):
            raise InstallError("Refusing a non-HTTPS tool download.")
        while chunk := response.read(1024 * 1024):
            size += len(chunk)
            if size > 256 * 1024 * 1024:
                raise InstallError("Tool download exceeds 256 MiB.")
            digest.update(chunk)
            output.write(chunk)
    if not size or (sha256 is not None and digest.hexdigest() != sha256):
        raise InstallError("Tool download is empty or its SHA-256 checksum does not match.")


@contextmanager
def file_lock(path: Path):
    """Locks survive crashes as files, but OS locks are always released on exit."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        stream.seek(0, 2)
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        try:
            if sys.platform == "win32":
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:  # Enables backend unit tests on Linux.
                import fcntl
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise InstallError(f"Another installation is using this location: {path.parent}") from error
        try:
            yield
        finally:
            stream.seek(0)
            if sys.platform == "win32":
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream, fcntl.LOCK_UN)


def run_process(command: list[str], cwd: Path, timeout: int | None = None) -> None:
    # Inherit the UI job's stdout/stderr pipes so long downloads stream live.
    kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if sys.platform == "win32" else {}
    try:
        with subprocess.Popen(command, cwd=cwd, stdin=subprocess.DEVNULL, **kwargs) as process:
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise InstallError(f"Tool timed out: {command[0]}")
        if code:
            raise ToolExitError(command[0], code)
    except OSError as error:
        raise InstallError(f"Cannot start {command[0]}: {error}") from error


class WindowsInstaller:
    def __init__(self, game: dict, install_dir: Path, data_dir: Path,
                 steamcmd: str = "", depot_downloader: str = "", validate: bool = True,
                 emit: Callable[[str], None] | None = None):
        self.game = game
        self.install_dir = install_dir.resolve()
        self.data_dir = data_dir.resolve()
        self.steamcmd = steamcmd
        self.depot_downloader = depot_downloader
        self.validate = validate
        self.emit = emit or (lambda text: print(text, flush=True))

    def plan(self) -> None:
        for index, text in enumerate((
            f"Native Windows x64 server: {self.game['name']} (Steam app {self.game['appId']})",
            f"Fresh server directory: {self.install_dir}",
            f"Managed tools and cache: {self.data_dir}",
            f"SteamCMD: {self.steamcmd or 'download Windows SteamCMD when needed'}",
            "Download Windows game files through anonymous SteamCMD" + (" with validation" if self.validate else ""),
            f"If missing, fetch only srcds_win64.exe from TF2 Windows depot {DONOR_DEPOT} using DepotDownloader",
            "Verify x64 PE launcher, engine DLLs, Steam runtime and game server DLL",
            "Write installation metadata and start-server.cmd; no firewall or service changes",
        ), 1):
            self.emit(f"{index}. {text}")

    def ensure_tool(self, kind: str) -> Path:
        steam = kind == "steamcmd"
        explicit = self.steamcmd if steam else self.depot_downloader
        folder = self.data_dir / ("steamcmd" if steam else "depotdownloader/3.4.0-windows-x64")
        name = "steamcmd.exe" if steam else "DepotDownloader.exe"
        executable = Path(explicit).resolve() if explicit else folder / name
        machine = 0x14C if steam else 0x8664
        if not (is_pe(executable, machine) or (steam and is_pe(executable))):
            if explicit:
                raise InstallError(f"Explicit {kind} is missing or has the wrong Windows architecture: {executable}")
            folder.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="srcds64-tool-", dir=folder.parent) as temporary:
                temp = Path(temporary)
                self.emit(f"Downloading {name}…")
                archive = temp / "tool.zip"
                download(STEAMCMD_URL if steam else DEPOT_URL, archive, None if steam else DEPOT_SHA256)
                unpacked = temp / "unpacked"
                extract_zip(archive, unpacked)
                if not is_pe(unpacked / name, machine):
                    raise InstallError(f"Downloaded archive does not contain a valid {name}.")
                if folder.exists():
                    backup = folder.with_name(folder.name + ".broken-" + uuid.uuid4().hex[:8])
                    folder.rename(backup)
                    self.emit(f"Preserved previous tool directory: {backup}")
                # TemporaryDirectory has restrictive Windows ACLs. Moving its
                # contents preserves those ACLs, including an elevated owner's
                # administrator-only access. Copy into a sibling created with
                # normal parent inheritance before publishing the tool folder.
                staged = folder.with_name(folder.name + ".new-" + uuid.uuid4().hex)
                try:
                    shutil.copytree(unpacked, staged)
                    staged.rename(folder)
                finally:
                    if staged.exists():
                        shutil.rmtree(staged)
        self.emit(f"Checking {executable}")
        if steam:
            self.run_steam([str(executable), "+quit"], executable.parent, timeout=300)
        else:
            run_process([str(executable), "--version"], executable.parent, timeout=300)
        return executable

    def run_steam(self, command: list[str], cwd: Path, timeout: int | None = None) -> None:
        # Valve's Windows bootstrap exits 7 after replacing itself. Retry the same
        # operation, but require an eventual zero exit and full file verification.
        for attempt in range(3):
            try:
                run_process(command, cwd, timeout)
                return
            except ToolExitError as error:
                if error.code != 7 or attempt == 2:
                    raise
                self.emit("SteamCMD restarted after an update; retrying…")
                time.sleep(2)

    def doctor(self) -> None:
        with file_lock(self.data_dir / ".manager.lock"):
            self.ensure_tool("steamcmd")
            self.ensure_tool("depotdownloader")
        self.emit("Windows tools are ready.")

    def check_destination(self) -> None:
        root = self.install_dir
        if root == Path(root.anchor) or root == Path.home().resolve():
            raise InstallError("Choose a dedicated, empty server folder.")
        protected = [self.data_dir]
        protected += [Path(p).resolve().parent for p in (self.steamcmd, self.depot_downloader) if p]
        for path in protected:
            if root == path or root in path.parents or path in root.parents:
                raise InstallError("The server folder must not overlap the tool/cache folders.")
        if root.exists() and (not root.is_dir() or any(root.iterdir())):
            raise InstallError(f"Server directory is not empty: {root}. Choose a new folder; existing files were preserved.")

    def ensure_launcher(self) -> None:
        launcher = self.install_dir / "srcds_win64.exe"
        if is_pe(launcher):
            return
        if launcher.exists():
            raise InstallError("Game download supplied an invalid srcds_win64.exe; refusing to overwrite it.")
        cache = self.data_dir / "donor/tf2-windows-x64"
        donor = cache / "srcds_win64.exe"
        if not is_pe(donor):
            tool = self.ensure_tool("depotdownloader")
            cache.mkdir(parents=True, exist_ok=True)
            filelist = cache / "runtime-files.txt"
            filelist.write_text("srcds_win64.exe\n", encoding="utf-8")
            self.emit(f"Downloading only the Windows x64 launcher from TF2 depot {DONOR_DEPOT}…")
            run_process([str(tool), "-app", DONOR_APP, "-depot", DONOR_DEPOT,
                         "-os", "windows", "-osarch", "64", "-dir", str(cache),
                         "-filelist", str(filelist), "-validate"], tool.parent)
            if not is_pe(donor):
                raise InstallError("The donor download did not provide a valid x64 launcher; partial cache preserved.")
        shutil.copy2(donor, launcher)

    def verify(self) -> bool:
        paths = [Path("srcds_win64.exe"), *(Path("bin/x64") / name for name in RUNTIME_DLLS),
                 Path(self.game["gameDirectory"]) / "bin/x64/server.dll"]
        passed = True
        for path in paths:
            valid = is_pe(self.install_dir / path)
            passed &= valid
            self.emit(f"[{'PASS' if valid else 'FAIL'}] Windows x64 PE: {path}")
        gameinfo = self.install_dir / self.game["gameDirectory"] / "gameinfo.txt"
        valid = gameinfo.is_file()
        passed &= valid
        self.emit(f"[{'PASS' if valid else 'FAIL'}] Game configuration: {gameinfo}")
        return passed

    def install(self) -> None:
        self.check_destination()  # Refuse before downloading tools or touching the destination.
        key = hashlib.sha256(str(self.install_dir).casefold().encode()).hexdigest()[:20]
        with file_lock(self.install_dir.parent / f".srcds64-{key}.lock"), file_lock(self.data_dir / ".manager.lock"):
            self.check_destination()
            tool = self.ensure_tool("steamcmd")
            self.install_dir.mkdir(parents=True, exist_ok=True)
            # Use SteamCMD's native host platform. Forcing "windows" on the current
            # Windows client can incorrectly return "Missing configuration".
            command = [str(tool), "+force_install_dir", str(self.install_dir),
                       "+login", "anonymous", "+app_info_update", "1", "+app_update", str(self.game["appId"])]
            if self.validate:
                command.append("validate")
            self.run_steam(command + ["+quit"], tool.parent)
            self.ensure_launcher()
            if not self.verify():
                raise InstallError("Windows runtime verification failed. Files were preserved; see failed checks above.")
            # The script's directory is resolved at runtime, without interpolating a user path into cmd.exe code.
            launch = (f'@echo off\r\nsetlocal DisableDelayedExpansion\r\ncd /d "%~dp0"\r\n'
                      f'srcds_win64.exe -console -game {self.game["gameDirectory"]} +map {self.game["defaultMap"]}\r\n'
                      'pause\r\n')
            (self.install_dir / "start-server.cmd").write_bytes(launch.encode("ascii"))
            marker = {"schemaVersion": 1, "platform": "windows-x64", "game": self.game["id"],
                      "appId": self.game["appId"], "installedAtUtc": datetime.now(timezone.utc).isoformat(),
                      "donorAppId": int(DONOR_APP), "donorDepotId": int(DONOR_DEPOT)}
            (self.install_dir / ".srcds64-manager.json").write_text(json.dumps(marker, indent=2), encoding="utf-8")
            self.emit(f"Installed successfully. To start the server, open: {self.install_dir / 'start-server.cmd'}")


def main(argv: list[str] | None = None) -> int:
    if sys.stdout is not None and hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Native Windows x64 Source server installer")
    parser.add_argument("action", choices=("plan", "doctor", "install", "verify"))
    parser.add_argument("--game", choices=[g["id"] for g in GAMES], default="hl2dm")
    parser.add_argument("--install-dir")
    parser.add_argument("--data-dir")
    parser.add_argument("--steamcmd", default="")
    parser.add_argument("--depot-downloader", default="")
    parser.add_argument("--yes", action="store_true")
    parser.add_argument("--no-validate", action="store_true")
    args = parser.parse_args(argv)
    if sys.platform != "win32":
        parser.error("This backend runs directly on Windows. Use the C++ srcds64 backend on Linux.")
    if args.action == "install" and not args.yes:
        parser.error("Noninteractive installation requires --yes.")
    game = next(g for g in GAMES if g["id"] == args.game)
    root = Path(args.install_dir) if args.install_dir else Path.home() / "servers" / f"{args.game}-server"
    data = Path(args.data_dir) if args.data_dir else Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "srcds64-manager"
    installer = WindowsInstaller(game, root, data, args.steamcmd, args.depot_downloader, not args.no_validate)
    try:
        if args.action == "verify":
            return 0 if installer.verify() else 1
        getattr(installer, args.action)()
        return 0
    except PermissionError as error:
        print(f"ERROR: Access denied: {error.filename or error}. Choose a tool/cache and server "
              "folder writable by your Windows user. Leave Tool and cache directory blank "
              "to use your LocalAppData folder. Previously administrator-created tool folders "
              "may need their permissions repaired or a new cache folder.", flush=True)
        return 1
    except (OSError, ValueError, InstallError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
