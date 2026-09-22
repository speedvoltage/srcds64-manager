#!/usr/bin/env python3
"""Assemble a native Windows portable ZIP on Linux or Windows.

Uses CPython's supported embeddable distribution and hash-locked Windows wheels.
No compiler, WSL, PyInstaller or system-wide Python install is needed by users.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.2.0"
PYTHON_VERSION = "3.13.15"
PYTHON_SHA256 = "d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf"
NAME = f"srcds64-manager-{VERSION}-windows-x64"


def main():
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="srcds64-package-") as temporary:
        work = Path(temporary)
        package = work / NAME
        runtime = package / "runtime"
        library = package / "lib"
        application = package / "app"
        for directory in (runtime, library, application):
            directory.mkdir(parents=True)
        archive = work / "python.zip"
        url = f"https://www.python.org/ftp/python/{PYTHON_VERSION}/python-{PYTHON_VERSION}-embed-amd64.zip"
        urllib.request.urlretrieve(url, archive)
        with archive.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != PYTHON_SHA256:
                raise RuntimeError("Embedded Python checksum mismatch")
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(runtime)
        (runtime / "python313._pth").write_text("python313.zip\n.\n../lib\n../app\nimport site\n", encoding="ascii")
        wheels = work / "wheels"
        subprocess.run([sys.executable, "-m", "pip", "download", "--dest", str(wheels),
                        "--platform", "win_amd64", "--python-version", "3.13", "--implementation", "cp",
                        "--only-binary=:all:", "--require-hashes", "-r", str(ROOT / "scripts/requirements-windows.lock")], check=True)
        for wheel in wheels.glob("*.whl"):
            with zipfile.ZipFile(wheel) as zipped:
                # These locked wheels contain import packages and .dist-info licenses at their root.
                if any(".data/" in name for name in zipped.namelist()):
                    raise RuntimeError(f"Wheel requires additional installation layout handling: {wheel.name}")
                zipped.extractall(library)
        shutil.copytree(ROOT / "interfaces/src/srcds64_ui", application / "srcds64_ui",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        shutil.copy(ROOT / "scripts/windows/launch.py", application / "launch.py")
        shutil.copy(ROOT / "scripts/windows/create-shortcuts.ps1", package / "Create shortcuts.ps1")
        for filename in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            shutil.copy(ROOT / filename, package / filename)
        shutil.copy(ROOT / "README.md", package / "README.md")
        shutil.copy(ROOT / "scripts/requirements-windows.lock", package / "DEPENDENCIES.txt")
        launchers = {
            "Create desktop shortcuts.cmd": '@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Create shortcuts.ps1"\npause\n',
            "Open GUI.cmd": '@echo off\nsetlocal DisableDelayedExpansion\nstart "" "%~dp0runtime\\pythonw.exe" "%~dp0app\\launch.py" gui\n',
            "Open TUI.cmd": '@echo off\nsetlocal DisableDelayedExpansion\n"%~dp0runtime\\python.exe" "%~dp0app\\launch.py" tui\npause\n',
            "Check app.cmd": '@echo off\nsetlocal DisableDelayedExpansion\n"%~dp0runtime\\python.exe" "%~dp0app\\launch.py" check\npause\n',
        }
        for filename, content in launchers.items():
            (package / filename).write_bytes(content.replace("\n", "\r\n").encode("ascii"))
        output = Path(shutil.make_archive(str(dist / NAME), "zip", work, NAME))
        with output.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        output.with_suffix(".zip.sha256").write_text(f"{digest}  {output.name}\n", encoding="ascii")
        print(f"Created {output} ({output.stat().st_size / 1024 / 1024:.1f} MiB)")


if __name__ == "__main__":
    main()
