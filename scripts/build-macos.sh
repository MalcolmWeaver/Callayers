#!/bin/bash
# Build a native renderer preview and run the real SDK checks on a matching host.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { printf 'Usage: bash scripts/build-macos.sh [--arch arm64|x86_64] [--open]\n'; }
arch=""
launch=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; arch="$2"; shift 2 ;;
        --open) launch=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if [[ "$(uname -s)" != Darwin ]]; then
    printf 'The native Mac preview must be compiled on macOS with full Xcode installed.\n' >&2
    exit 1
fi
host_arch="$(uname -m)"
if [[ "$(/usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || true)" == 1 ]]; then
    host_arch=arm64
fi
arch="${arch:-$host_arch}"
case "$arch" in arm64|x86_64) ;; *) usage >&2; exit 2 ;; esac
if [[ "$launch" == 1 && "$arch" != "$host_arch" ]]; then
    printf 'Use --open with the host architecture (%s). Cross-compiled products require their own Mac verification.\n' "$host_arch" >&2
    exit 2
fi
if ! xcrun --find xcodebuild >/dev/null 2>&1 || ! xcodebuild -version >/dev/null 2>&1; then
    printf 'Install and open full Xcode, accept its license, and select Xcode in Settings > Locations > Command Line Tools.\n' >&2
    exit 1
fi
command -v python3 >/dev/null || { printf 'Python 3 is required to acquire the renderer SDK.\n' >&2; exit 1; }
sdk="$(python3 "$root/scripts/acquire-macos-renderer.py" --arch "$arch")"
build="$root/build/macos-$arch"
mkdir -p "$build"
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/App/WorkspaceModels.swift" "$root/macos/App/WorkspaceStore.swift" \
    "$root/macos/Tests/WorkspaceChecks.swift" -o "$build/WorkspaceChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/WorkspaceChecks" 2>&1 | tee "$build/workspace-checks.log"
fi
xcodebuild -project "$root/macos/Callayers.xcodeproj" \
    -scheme Callayers -configuration Debug -destination 'generic/platform=macOS' \
    -derivedDataPath "$build" ARCHS="$arch" ONLY_ACTIVE_ARCH=YES \
    ULTRALIGHT_ROOT="$sdk" build 2>&1 | tee "$build/build.log"
products="$build/Build/Products/Debug"
app="$products/Callayers Renderer Preview.app"
/usr/bin/codesign --verify --deep --strict "$app"
if [[ "$arch" == "$host_arch" ]]; then
    python3 "$root/macos/Tests/run-renderer-checks.py" \
        "$products/RendererChecks" "$app/Contents/Resources" "$build/renderer-preview.png" \
        2>&1 | tee "$build/renderer-checks.log"
else
    printf 'Built %s; renderer execution checks require a %s Mac.\n' "$arch" "$arch"
fi
printf '\nMac preview: %s\n' "$app"
if [[ "$launch" == 1 ]]; then open "$app"; fi
