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
