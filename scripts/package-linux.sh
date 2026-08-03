#!/usr/bin/env bash

set -Eeuo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-$project_root/build/srcds64}"

if [[ ! -x "$binary" ]]; then
    printf 'Executable not found or not executable: %s\n' "$binary" >&2
    exit 1
fi

version="$($binary --version | awk 'NR == 1 { print $2 }')"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Unable to determine a semantic version from %s --version\n' "$binary" >&2
    exit 1
fi

archive_name="srcds64-manager-${version}-linux-x86_64"
dist="$project_root/dist"
staging="$dist/$archive_name"
archive="$dist/$archive_name.tar.gz"

rm -rf -- "$staging"
mkdir -p -- "$staging"

install -m 0755 "$binary" "$staging/srcds64"
install -m 0755 "$project_root/scripts/install-dependencies.sh" "$staging/install-dependencies.sh"
install -m 0644 "$project_root/README.md" "$staging/README.md"
install -m 0644 "$project_root/LICENSE" "$staging/LICENSE"
install -m 0644 "$project_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"

rm -f -- "$archive" "$archive.sha256"
tar -C "$dist" -czf "$archive" "$archive_name"
sha256sum "$archive" > "$archive.sha256"
rm -rf -- "$staging"

printf '%s\n' "$archive"
printf '%s\n' "$archive.sha256"
