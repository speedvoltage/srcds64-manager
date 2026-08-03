#!/usr/bin/env bash

set -Eeuo pipefail

mode="${1:-build}"

case "$mode" in
    build|runtime)
        ;;
    *)
        printf 'Usage: %s [build|runtime]\n' "$0" >&2
        exit 2
        ;;
esac

if [[ ! -r /etc/os-release ]]; then
    printf 'Unable to identify this Linux distribution.\n' >&2
    exit 1
fi

. /etc/os-release

if (( EUID == 0 )); then
    privilege=()
elif command -v sudo >/dev/null 2>&1; then
    privilege=(sudo)
else
    printf 'Run this script as root or install sudo.\n' >&2
    exit 1
fi

case " ${ID:-} ${ID_LIKE:-} " in
    *" debian "*|*" ubuntu "*)
        family="debian"
        ;;
    *" arch "*)
        family="arch"
        ;;
    *)
        printf 'Unsupported Linux distribution: ID=%s ID_LIKE=%s\n' \
            "${ID:-unknown}" "${ID_LIKE:-none}" >&2
        printf 'Supported families: Debian/Ubuntu and Arch Linux.\n' >&2
        exit 1
        ;;
esac

if [[ "$family" == "debian" ]]; then
    packages=(
        ca-certificates
        tar
        gzip
        unzip
        lib32gcc-s1
        lib32stdc++6
    )

    if [[ "$mode" == "build" ]]; then
        packages+=(
            build-essential
            cmake
            ninja-build
            python3
            qt6-base-dev
        )
    else
        packages+=(
            libqt6network6
        )
    fi

    "${privilege[@]}" apt-get update
    "${privilege[@]}" apt-get install -y --no-install-recommends "${packages[@]}"
else
    if ! command -v pacman >/dev/null 2>&1 || ! command -v pacman-conf >/dev/null 2>&1; then
        printf 'This system identifies as Arch-based, but pacman or pacman-conf is unavailable.\n' >&2
        exit 1
    fi

    if ! pacman-conf --repo-list 2>/dev/null | grep -Fxq 'multilib'; then
        cat >&2 <<'EOF'
The Arch Linux multilib repository is not enabled.
SteamCMD requires 32-bit runtime libraries from multilib.

Enable this section in /etc/pacman.conf:

[multilib]
Include = /etc/pacman.d/mirrorlist

Then rerun this script. It will perform the required full system upgrade.
EOF
        exit 1
    fi

    packages=(
        ca-certificates
        tar
        gzip
        unzip
        lib32-gcc-libs
        qt6-base
    )

    if [[ "$mode" == "build" ]]; then
        packages+=(
            base-devel
            cmake
            ninja
            python
        )
    fi

    printf 'Arch Linux requires complete system upgrades; running pacman -Syu.\n'
    "${privilege[@]}" pacman -Syu --needed --noconfirm "${packages[@]}"
fi
