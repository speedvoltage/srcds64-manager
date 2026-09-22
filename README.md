# SRCDS 64 Manager

## New terminal and desktop interfaces

The **Textual TUI** and **Qt desktop GUI** support Linux
and Windows. Both provide game selection, installation previews, tool setup,
fresh installation, verification, live output, and shared saved settings.
Windows now includes a native Windows installer; WSL is not required. A portable
Windows ZIP includes Python and both interfaces. Extract the ZIP and run
`Open GUI.cmd` or `Open TUI.cmd`. Administrator rights are not required.

```sh
python -m pip install -e '.[tui,gui]'
srcds64-tui
srcds64-gui
```

Linux uses the original C++ backend described below. Windows uses the bundled
native backend and does not require a C++ build.

SRCDS 64 Manager automatically creates fresh 64-bit Linux Source Dedicated Server installations.

It installs the selected game through SteamCMD, obtains the small set of missing 64-bit runtime files from the Team Fortress 2 Linux server depot, creates the required library aliases, and verifies the finished server.

## Supported games

| ID | Game | Steam app | Game directory | Default map |
|---|---|---:|---|---|
| `css` | Counter-Strike: Source | 232330 | `cstrike` | `de_dust2` |
| `dods` | Day of Defeat: Source | 232290 | `dod` | `dod_anzio` |
| `hl2dm` | Half-Life 2: Deathmatch | 232370 | `hl2mp` | `dm_lockdown` |
| `hldm` | Half-Life Deathmatch: Source | 255470 | `hl1mp` | `crossfire` |

## Quick start

Install the runtime dependencies on Debian or Ubuntu:

```sh
./install-dependencies.sh runtime
```

Check or install the managed tools:

```sh
./srcds64 doctor
```

### Example Installation:
Install an HL2DM server:

```sh
./srcds64 install \
  --game hl2dm \
  --install-dir "$HOME/servers/hl2dm-server"
```

The installer prints the exact server directory and direct SRCDS launch command after verification succeeds.

## Build from source

Install the build dependencies on Debian or Ubuntu:

```sh
./scripts/install-dependencies.sh build
```

Build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to:

```text
build/srcds64
```

Verify no issue is raised:

```sh
ctest --test-dir build --output-on-failure
```

## Commands

List the supported games:

```sh
./build/srcds64 games
```

Discover, install, and validate SteamCMD and DepotDownloader:

```sh
./build/srcds64 doctor
```

Preview an installation without changing the server destination:

```sh
./build/srcds64 plan \
  --game hl2dm \
  --install-dir "$HOME/servers/hl2dm-server"
```

Install a fresh server:

```sh
./build/srcds64 install \
  --game hl2dm \
  --install-dir "$HOME/servers/hl2dm-server"
```

Verify an installed server:

```sh
./build/srcds64 verify \
  --game hl2dm \
  --install-dir "$HOME/servers/hl2dm-server"
```

Show every option:

```sh
./build/srcds64 --help
```

## Starting the installed server

For HL2DM:

```sh
cd "$HOME/servers/hl2dm-server"
./srcds_run_64 -game hl2mp +map dm_lockdown
```

Equivalent commands for the other supported games:

```sh
./srcds_run_64 -game cstrike +map de_dust2
./srcds_run_64 -game dod +map dod_anzio
./srcds_run_64 -game hl1mp +map crossfire
```

If the normal command reports missing shared libraries, export the server library paths first:

```sh
export LD_LIBRARY_PATH="$PWD/bin:$PWD/bin/linux64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./srcds_run_64 -game hl2mp +map dm_lockdown
```

Server configuration, launch parameters, process supervision, firewall rules, and systemd services are intentionally outside version 1.0's scope.

## How the 64-bit runtime is assembled

The affected game depots provide their 64-bit game libraries but omit parts of the dedicated-server launcher runtime.

The manager downloads only these files from TF2 Linux server depot `232256`:

```text
srcds_linux64
srcds_run_64
bin/linux64/libsteam_api.so
```

The verified ELF64 `steamclient.so` comes from SteamCMD's `linux64` runtime. The final reusable donor cache contains:

```text
srcds_linux64
srcds_run_64
libsteam_api.so
steamclient.so
```

The bounded donor cache is approximately 45 MB with the currently tested depot manifest. We __never__ have to download a complete TF2 server installation.

DepotDownloader is used because anonymous SteamCMD `download_depot` requests can fail with `Missing configuration`. Partial donor downloads are preserved and reused on the next attempt, but you can always remove them and start fresh again if need be.

## Paths

Default installation directory:

```text
<selected-user-home>/servers/<game>-server
```

Default application data directory:

```text
<selected-user-home>/.local/share/srcds64-manager
```

Managed tools:

```text
<application-data>/steamcmd
<application-data>/depotdownloader/3.4.0
```

Reusable donor cache:

```text
<application-data>/donor/tf2-linux64
```

Interrupted donor download:

```text
<donor-cache>.partial
```

Verified donor activation directory:

```text
<donor-cache>.ready
```

Every important path can be overridden from the command line.

## Account handling

Without `--owner`, the terminal asks whether to use the current account and defaults to yes. If the terminal is non-interactive, the execution uses the invoking account.

A different existing Linux account can be selected with:

```sh
sudo ./build/srcds64 install --owner steam --game hl2dm
```

The application does not create Linux accounts. Running installation commands as another account requires root privileges.

## Dependencies

Runtime:

- Linux x86-64
- Qt 6 Core and Network libraries
- CA certificates
- `tar`, `gzip`, and `unzip`
- 32-bit GCC and C++ runtime libraries required by SteamCMD

Build:

- CMake 3.21 or newer
- C++20 compiler
- Qt 6.4 or newer with Core and Network
- Ninja or another CMake-supported build system

The dependency helper supports Debian, Ubuntu, and distributions that declare Debian compatibility through `/etc/os-release`. Other distributions must install equivalent packages manually.

## Third-party software

SteamCMD and DepotDownloader are downloaded only when needed and remain separate programs. They are not included in this source repository.