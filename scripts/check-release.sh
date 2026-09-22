#!/usr/bin/env bash

set -Eeuo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

bash -n scripts/install-dependencies.sh
bash -n scripts/package-linux.sh

python3 - <<'PY'
import json
import re
from pathlib import Path

root = Path('.')
cmake = (root / 'CMakeLists.txt').read_text()
match = re.search(r'project\(srcds64-manager VERSION ([0-9]+\.[0-9]+\.[0-9]+)', cmake)
if not match:
    raise SystemExit('Unable to read the project version from CMakeLists.txt')

version = match.group(1)
if version != '1.0.0':
    raise SystemExit(f'Unexpected release version: {version}')

entries = json.loads((root / 'resources/games.json').read_text())
expected = {'css', 'dods', 'hl2dm', 'hldm'}
actual = {entry['id'] for entry in entries}
if actual != expected:
    raise SystemExit(f'Unexpected game catalog: {sorted(actual)}')

dependency_helper = (root / 'scripts/install-dependencies.sh').read_text()
for required_snippet in (
    'pacman-conf --repo-list',
    "grep -Fxq 'multilib'",
    'lib32-gcc-libs',
    '[multilib]',
):
    if required_snippet not in dependency_helper:
        raise SystemExit(f'Missing Arch dependency preflight: {required_snippet}')

required = {
    'README.md',
    'CHANGELOG.md',
    'CONTRIBUTING.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
}
missing = sorted(path for path in required if not (root / path).is_file())
if missing:
    raise SystemExit(f'Missing release files: {missing}')

source = '\n'.join(path.read_text() for path in (root / 'src').rglob('*') if path.is_file())
for forbidden in ('LaunchOptions', 'Installer::launch', 'srcds64 launch'):
    if forbidden in source:
        raise SystemExit(f'Obsolete managed launch code remains: {forbidden}')
PY

printf 'Release checks passed.\n'
