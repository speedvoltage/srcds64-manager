from pathlib import Path
import struct
import zipfile

import pytest

from srcds64_ui.model import GAMES
from srcds64_ui import windows_backend as backend


def write_pe(path, machine=0x8664):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<H", data, 132, machine)
    struct.pack_into("<H", data, 148, 240 if machine == 0x8664 else 224)
    struct.pack_into("<H", data, 152, 0x20B if machine == 0x8664 else 0x10B)
    path.write_bytes(data)


def fake_server(root, game):
    write_pe(root / "srcds_win64.exe")
    for name in backend.RUNTIME_DLLS:
        write_pe(root / "bin/x64" / name)
    write_pe(root / game["gameDirectory"] / "bin/x64/server.dll")
    (root / game["gameDirectory"] / "gameinfo.txt").write_text("GameInfo {}")


@pytest.fixture
def installer(tmp_path):
    return backend.WindowsInstaller(GAMES[2], tmp_path / "my server", tmp_path / "data", emit=lambda line: None)


def test_pe_rejects_32bit_truncated_and_invalid_offsets(tmp_path):
    path = tmp_path / "sample.exe"
    write_pe(path)
    assert backend.is_pe(path)
    write_pe(path, 0x14C)
    assert not backend.is_pe(path)
    assert backend.is_pe(path, 0x14C)
    path.write_bytes(b"MZ")
    assert not backend.is_pe(path)
    write_pe(path)
    data = bytearray(path.read_bytes())
    struct.pack_into("<I", data, 60, 0xFFFFFFFF)
    path.write_bytes(data)
    assert not backend.is_pe(path)


@pytest.mark.parametrize("name", ["../escape.exe", "/root.exe", "C:/bad.exe", "folder/../../bad", "bin/file:stream", "NUL.txt", "bad. "])
def test_zip_rejects_windows_unsafe_names(tmp_path, name):
    archive = tmp_path / "bad.zip"
    with zipfile.ZipFile(archive, "w") as zipped:
        zipped.writestr(name, b"bad")
    with pytest.raises(backend.InstallError, match="Unsafe"):
        backend.extract_zip(archive, tmp_path / "out")
    assert not (tmp_path / "out").exists()


def test_zip_extracts_safe_and_rejects_symlink(tmp_path):
    archive = tmp_path / "tool.zip"
    with zipfile.ZipFile(archive, "w") as zipped:
        zipped.writestr("bin/tool.exe", b"tool")
    backend.extract_zip(archive, tmp_path / "out")
    assert (tmp_path / "out/bin/tool.exe").read_bytes() == b"tool"
    with zipfile.ZipFile(archive, "w") as zipped:
        item = zipfile.ZipInfo("link")
        item.external_attr = 0o120777 << 16
        zipped.writestr(item, "elsewhere")
    with pytest.raises(backend.InstallError):
        backend.extract_zip(archive, tmp_path / "out")


def test_plan_is_read_only(installer):
    installer.plan()
    assert not installer.data_dir.exists()
    assert not installer.install_dir.exists()


def test_refuses_existing_server_before_running_tools(installer, monkeypatch):
    installer.install_dir.mkdir()
    saved = installer.install_dir / "server.cfg"
    saved.write_text("keep me")
    monkeypatch.setattr(installer, "ensure_tool", lambda _: pytest.fail("Should not run tools"))
    with pytest.raises(backend.InstallError, match="not empty"):
        installer.install()
    assert saved.read_text() == "keep me"


def test_refuses_overlapping_data_and_server(installer):
    installer.install_dir = installer.data_dir / "server"
    with pytest.raises(backend.InstallError, match="overlap"):
        installer.check_destination()


def test_locks_exclude_overlapping_jobs_and_release(tmp_path):
    path = tmp_path / ".lock"
    with backend.file_lock(path):
        with pytest.raises(backend.InstallError, match="Another installation"):
            with backend.file_lock(path):
                pass
    with backend.file_lock(path):
        pass


@pytest.mark.parametrize("game", GAMES)
def test_verification_requires_native_x64_game_and_engine(tmp_path, game):
    installer = backend.WindowsInstaller(game, tmp_path / game["id"], tmp_path / "data", emit=lambda _: None)
    fake_server(installer.install_dir, game)
    assert installer.verify()
    write_pe(installer.install_dir / game["gameDirectory"] / "bin/x64/server.dll", 0x14C)
    assert not installer.verify()


def test_install_downloads_windows_game_and_writes_launcher(installer, monkeypatch, tmp_path):
    tool = tmp_path / "tools/steamcmd.exe"
    tool.parent.mkdir()
    monkeypatch.setattr(installer, "ensure_tool", lambda _: tool)
    calls = []

    def run(command, cwd, timeout=None):
        calls.append(command)
        fake_server(installer.install_dir, installer.game)

    monkeypatch.setattr(backend, "run_process", run)
    installer.install()
    assert "+@sSteamCmdForcePlatformType" not in calls[0]
    assert "+app_info_update" in calls[0]
    assert str(installer.install_dir) in calls[0]
    assert "validate" in calls[0]
    script = (installer.install_dir / "start-server.cmd").read_text()
    assert 'cd /d "%~dp0"' in script
    assert "srcds_win64.exe -console -game hl2mp +map dm_lockdown" in script
    assert (installer.install_dir / ".srcds64-manager.json").is_file()


def test_launcher_download_is_bounded_and_reused(installer, monkeypatch, tmp_path):
    installer.install_dir.mkdir()
    tool = tmp_path / "DepotDownloader.exe"
    monkeypatch.setattr(installer, "ensure_tool", lambda _: tool)
    calls = []

    def run(command, cwd, timeout=None):
        calls.append(command)
        listing = Path(command[command.index("-filelist") + 1])
        assert listing.read_text() == "srcds_win64.exe\n"
        assert command[command.index("-depot") + 1] == "232255"
        write_pe(listing.parent / "srcds_win64.exe")

    monkeypatch.setattr(backend, "run_process", run)
    installer.ensure_launcher()
    assert backend.is_pe(installer.install_dir / "srcds_win64.exe")
    (installer.install_dir / "srcds_win64.exe").unlink()
    installer.ensure_launcher()
    assert len(calls) == 1


def test_bad_explicit_tool_is_not_silently_replaced(installer):
    installer.steamcmd = str(installer.data_dir / "missing.exe")
    with pytest.raises(backend.InstallError, match="Explicit"):
        installer.ensure_tool("steamcmd")
    assert not installer.data_dir.exists()


def test_failed_game_download_never_writes_success_marker(installer, monkeypatch, tmp_path):
    monkeypatch.setattr(installer, "ensure_tool", lambda _: tmp_path / "steamcmd.exe")

    def fail(*args, **kwargs):
        raise backend.InstallError("download failed")

    monkeypatch.setattr(backend, "run_process", fail)
    with pytest.raises(backend.InstallError, match="download failed"):
        installer.install()
    assert not (installer.install_dir / ".srcds64-manager.json").exists()


def test_steam_update_exit_retries_but_never_counts_as_success(installer, monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(backend.time, "sleep", lambda _: None)

    def restart_once(command, cwd, timeout=None):
        calls.append(command)
        if len(calls) == 1:
            raise backend.ToolExitError(command[0], 7)

    monkeypatch.setattr(backend, "run_process", restart_once)
    installer.run_steam(["steamcmd.exe", "+quit"], tmp_path)
    assert len(calls) == 2
    calls.clear()

    def always_fail(command, cwd, timeout=None):
        calls.append(command)
        raise backend.ToolExitError(command[0], 7)

    monkeypatch.setattr(backend, "run_process", always_fail)
    with pytest.raises(backend.ToolExitError):
        installer.run_steam(["steamcmd.exe", "+quit"], tmp_path)
    assert len(calls) == 3


def test_cached_steamcmd_self_update_to_x64_is_preserved(installer, monkeypatch):
    executable = installer.data_dir / "steamcmd/steamcmd.exe"
    write_pe(executable)
    monkeypatch.setattr(backend, "download", lambda *args: pytest.fail("Do not replace an updated x64 SteamCMD"))
    monkeypatch.setattr(backend, "run_process", lambda *args, **kwargs: None)
    assert installer.ensure_tool("steamcmd") == executable


def test_tool_archive_installs_and_cleans_staging(installer, monkeypatch, tmp_path):
    source = tmp_path / 'source.exe'
    write_pe(source, 0x14C)

    def download(url, destination, checksum):
        with zipfile.ZipFile(destination, 'w') as archive:
            archive.write(source, 'steamcmd.exe')

    monkeypatch.setattr(backend, 'download', download)
    monkeypatch.setattr(backend, 'run_process', lambda *args, **kwargs: None)
    executable = installer.ensure_tool('steamcmd')
    assert backend.is_pe(executable, 0x14C)
    assert list(installer.data_dir.iterdir()) == [executable.parent]
