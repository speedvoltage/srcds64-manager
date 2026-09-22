"""Portable entry point, with a visible error report if GUI startup fails."""
from pathlib import Path
import sys
import traceback


def check():
    import asyncio
    import subprocess
    from srcds64_ui.model import Settings
    print("Checking the native Windows application (no downloads)…", flush=True)
    command = Settings().command("plan")
    assert "wsl.exe" not in command
    result = subprocess.run(command, check=True, capture_output=True, text=True, encoding="utf-8")
    print(result.stdout)
    from PySide6.QtWidgets import QApplication
    from srcds64_ui.gui import ManagerWindow
    from srcds64_ui.tui import ManagerApp
    qt = QApplication([])
    window = ManagerWindow(Settings())
    window.show()
    qt.processEvents()
    window.close()

    async def terminal():
        app = ManagerApp(Settings())
        async with app.run_test(size=(100, 35)) as pilot:
            await pilot.click("#plan")
            for _ in range(100):
                await pilot.pause(.05)
                if app.job is None:
                    break
            assert app.job is None, "Terminal preview did not finish"
            from textual.widgets import Static
            assert "Completed successfully" in str(app.query_one("#status", Static).render())
    asyncio.run(terminal())
    print("PASS: Windows backend, desktop GUI and terminal UI. Ready for an installation test.", flush=True)


def main():
    if sys.stdout is not None and hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    mode = sys.argv[1] if len(sys.argv) > 1 else "gui"
    if mode == "check":
        check()
    elif mode == "gui":
        from srcds64_ui.gui import main as gui
        gui()
    elif mode == "tui":
        from srcds64_ui.tui import main as tui
        tui()
    else:
        raise ValueError(f"Unknown launch mode: {mode}")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        detail = traceback.format_exc()
        log = Path(__file__).resolve().parents[1] / "startup-error.txt"
        try:
            log.write_text(detail, encoding="utf-8")
        except OSError:
            pass
        if sys.stderr is not None:
            print(detail, file=sys.stderr)
        if sys.platform == "win32":
            import ctypes
            ctypes.windll.user32.MessageBoxW(None, f"The application could not start.\n\nDetails: {log}\n\n{detail[-1500:]}",
                                            "SRCDS 64 Manager", 0x10)
        raise SystemExit(1)
