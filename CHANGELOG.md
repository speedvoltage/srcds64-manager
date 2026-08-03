# Changelog

## 1.0.0

- Promoted the tested fresh-install pipeline to the first stable release.
- Kept support for Counter-Strike: Source, Day of Defeat: Source, Half-Life 2: Deathmatch, and Half-Life Deathmatch: Source.
- Removed the manager-owned SRCDS launch command and its launch-specific options.
- Added a clear post-install message with the server directory, normal SRCDS command, and optional `LD_LIBRARY_PATH` fallback.
- Added shell-safe quoting to generated post-install commands.
- Rejected unknown commands before account or path prompts.
- Removed stale version-specific wording from legacy-cache diagnostics.
- Added CTest smoke tests for version reporting and the embedded game catalog.
- Added a Linux build workflow and release-archive packaging script.
- Reworked the README for source builds, prebuilt releases, direct SRCDS startup, paths, dependencies, and tested status.
- Added contribution, testing, and GitHub release documentation.
- Extended the dependency helper to Debian/Ubuntu and Arch Linux families with native package names, package-manager handling, and an explicit Arch `multilib` preflight check.

## 0.1.3

- Corrected TF2 donor handling after confirming that depot `232256` provides `bin/steamclient.so` as a 32-bit ELF file.
- Stopped requesting the depot's 32-bit `steamclient.so`.
- Reused a verified ELF64 `linux64/steamclient.so` from SteamCMD instead.
- Added automatic managed SteamCMD runtime installation when an external SteamCMD lacks a usable 64-bit client library.
- Reduced the DepotDownloader file list from four files to three.
- Added explicit per-file donor validation errors.
- Added atomic donor-cache activation through a `.ready` directory.
- Preserved invalid non-empty donor caches instead of overwriting them.
- Stopped creating an empty final donor-cache directory before a successful download.
- Expanded `doctor` to validate and display the SteamCMD ELF64 client library.

## 0.1.2

- Removed the automatic full TF2 dedicated server fallback.
- Added managed SteamRE DepotDownloader discovery, installation, and validation.
- Pinned the managed helper to DepotDownloader 3.4.0 for Linux x86-64.
- Added GitHub release metadata and SHA-256 verification when the digest is available.
- Limited TF2 donor acquisition to the required files from depot `232256`.
- Preserved partial donor downloads for retry instead of restarting from zero.
- Moved donor acquisition before creation of the fresh game destination.
- Added `--depot-downloader` for an explicit external executable.
- Expanded `doctor` to validate or install both SteamCMD and DepotDownloader.
- Added a `doctor` warning for the obsolete full TF2 cache.
- Added Debian and Ubuntu build and runtime dependency helper modes.
- Added third-party notices.

## 0.1.1

- Forced SteamCMD game downloads to the Linux platform.
- Refreshed Steam application metadata before downloads.
- Retried managed SteamCMD after clearing stale application metadata when `Missing configuration` was reported.
- Corrected process timeout reporting.

## 0.1.0

- Added the terminal-first Qt 6 installer foundation.
- Added fresh installations for CSS, DOD:S, HL2DM, and HLDM:S.
- Added configurable account, installation, application-data, and donor-cache paths.
- Added SteamCMD discovery, managed installation, installation planning, verification, and reusable donor caching.
