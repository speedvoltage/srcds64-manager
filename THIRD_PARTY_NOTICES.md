# Third-party notices

## DepotDownloader

srcds64-manager can download and execute the official Linux x86-64 release of DepotDownloader as a separate managed tool when the TF2 donor cache is missing.

- Project: SteamRE/DepotDownloader
- Managed release pinned by srcds64-manager 1.0.0: 3.4.0
- License: GNU General Public License version 2
- Source and license: https://github.com/SteamRE/DepotDownloader

DepotDownloader is not included in the srcds64-manager source or binary archives. It is downloaded from the official GitHub release on demand. When GitHub supplies a SHA-256 digest in its release metadata, the digest is verified before extraction. The executable is always validated after extraction and must report a supported version.

## SteamCMD

srcds64-manager can download SteamCMD from Valve's official Steam CDN when no usable installation is found.

- Documentation: https://developer.valvesoftware.com/wiki/SteamCMD

SteamCMD is not included in the srcds64-manager source or binary archives.


## Portable Windows interfaces

The portable ZIP also bundles the unmodified official CPython embeddable runtime,
PySide6 Essentials / Qt, Shiboken6, Textual, and their Python dependencies.
Versions and hashes are recorded in `scripts/requirements-windows.lock`.
CPython's license is included under `runtime`; wheel license files and metadata
are retained under `lib`. Qt/PySide6 LGPL license terms apply to the corresponding
libraries; they remain separate replaceable files. Sources are available from
https://code.qt.io/ and https://pypi.org/project/PySide6-Essentials/.

SteamCMD and DepotDownloader are not bundled in the portable ZIP.
