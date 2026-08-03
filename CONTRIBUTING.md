# Contributing

## Scope

Version 1.x is intentionally focused on safe fresh installation and verification. Changes that add server supervision, configuration editing, addon management, or repair of existing installations should be proposed separately and must not weaken fresh-install safety.

## Build

```sh
./scripts/install-dependencies.sh build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Requirements

- Keep the core usable without a GUI.
- Do not add hard-coded usernames or home directories.
- Do not silently download a complete TF2 server.
- Do not overwrite a non-empty server destination.
- Preserve partial donor downloads when retry is safe.
- Validate executable and ELF architecture before activating downloaded runtime files.
- Keep game-specific values in `resources/games.json` where possible.
- Update `CHANGELOG.md` for user-visible changes.

## Pull requests

Include:

- the Linux distribution and architecture used for testing;
- the tested game identifier;
- whether SteamCMD and DepotDownloader were pre-existing or managed;
- the complete verification result;
- confirmation that the donor cache remained bounded and no full TF2 installation was downloaded.
