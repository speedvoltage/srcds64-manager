"""Install Linux launchers and the icon for the current user (no sudo).

Run with the Python environment where the interfaces are installed.
"""
import os
from pathlib import Path
import shutil
import sys
from srcds64_ui import __file__ as package_file

if sys.platform != 'linux':
    raise SystemExit('This desktop installer is for Linux.')
data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
icons = data / 'icons/hicolor/scalable/apps'
icons.mkdir(parents=True, exist_ok=True)
shutil.copyfile(Path(package_file).parent / 'assets/srcds64-manager.svg', icons / 'srcds64-manager.svg')
applications = data / 'applications'
applications.mkdir(parents=True, exist_ok=True)
# Desktop Entry Exec quoting is separate from shell quoting.
executable = sys.executable.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
for suffix, name, terminal, module in (
    ('manager', 'SRCDS 64 Manager', 'false', 'gui'),
    ('terminal', 'SRCDS 64 Terminal', 'true', 'tui'),
):
    entry = applications / f'srcds64-{suffix}.desktop'
    entry.write_text(f'''[Desktop Entry]
Type=Application
Name={name}
Comment=Install and verify Source dedicated servers
Exec="{executable}" -m srcds64_ui.{module}
Icon=srcds64-manager
Terminal={terminal}
Categories=Game;Utility;
StartupWMClass=srcds64-manager
''')
    print(f'Installed {entry}')
