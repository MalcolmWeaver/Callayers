#!/bin/bash
# Local native package; public distribution additionally requires Developer ID
# signing/notarization and a configured release host.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch="$(uname -m)"
team=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) arch="$2"; shift 2 ;;
        --team) team="$2"; shift 2 ;;
        *) printf 'Usage: bash scripts/package-macos.sh [--arch arm64|x86_64] [--team TEAM_ID]\n' >&2; exit 2 ;;
    esac
done
case "$arch" in arm64|x86_64) ;; *) exit 2 ;; esac
args=(--arch "$arch")
if [[ -n "$team" ]]; then args+=(--team "$team"); fi
bash "$root/scripts/build-macos.sh" "${args[@]}"
build="$root/build/macos-$arch"
if [[ -n "$team" ]]; then build="$root/build/macos-camera-$arch"; fi
app="$build/Build/Products/Debug/Callayers.app"
/usr/bin/codesign --verify --deep --strict "$app"
version="$(/usr/libexec/PlistBuddy -c Print:CFBundleShortVersionString "$app/Contents/Info.plist")"
case "$version" in *[!0-9.]*|'') printf 'Invalid app version\n' >&2; exit 1 ;; esac
mkdir -p "$root/build/artifacts"
archive="$root/build/artifacts/Callayers-$version-$arch.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
/usr/bin/unzip -t "$archive" > "$build/package-check.log"
printf 'Mac app package: %s\n' "$archive"
