r"""Launch an installed server locally and require a valid A2S_INFO response.

Run with the bundled runtime: runtime\python.exe test_server.py SERVER_DIR GAME
Uses the VM host address; no firewall changes or host port forwarding. Stops only the process this test starts.
"""
from pathlib import Path
import socket
import subprocess
import sys
import time

from srcds64_ui.model import GAMES


def main():
    root = Path(sys.argv[1]).resolve()
    game = next(g for g in GAMES if g["id"] == sys.argv[2])
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 27035
    host = socket.gethostbyname(socket.gethostname())
    args = [str(root / "srcds_win64.exe"), "-console", "-game", game["gameDirectory"],
            "-ip", host, "-port", str(port), "-insecure", "-nocrashdialog",
            "-condebug", "-conclearlog", "+sv_lan", "0", "+sv_use_steam_networking", "0", "+map", game["defaultMap"], "+maxplayers", "2"]
    print(f"Testing {game['id']} on {host}:{port}", flush=True)
    process = subprocess.Popen(args, cwd=root)
    request = b"\xff\xff\xff\xffTSource Engine Query\0"
    try:
        deadline = time.monotonic() + 120
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(1)
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"Server exited before responding: {process.returncode}")
                sock.sendto(request, (host, port))
                try:
                    response = sock.recv(65535)
                    if response[:5] == b"\xff\xff\xff\xffA":
                        sock.sendto(request + response[5:9], (host, port))
                        response = sock.recv(65535)
                    if response[:5] != b"\xff\xff\xff\xffI":
                        continue
                    values = response[6:].split(b"\0", 4)
                    name, map_name, directory, description = (value.decode("utf-8", errors="replace") for value in values[:4])
                    assert map_name == game["defaultMap"], (map_name, game["defaultMap"])
                    assert directory == game["gameDirectory"], directory
                    print(f"PASS: {game['name']} native Windows x64 server answered A2S_INFO on map {map_name} ({name}).", flush=True)
                    return
                except (TimeoutError, ConnectionResetError):
                    time.sleep(.1)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
    raise RuntimeError("Server did not answer A2S_INFO within 120 seconds. Check the game's console.log.")


if __name__ == "__main__":
    main()
