#!/bin/bash
# Build a native renderer preview and run the real SDK checks on a matching host.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { printf 'Usage: bash scripts/build-macos.sh [--arch arm64|x86_64] [--team TEAM_ID] [--open]\n'; }
arch=""
launch=0
team=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; arch="$2"; shift 2 ;;
        --open) launch=1; shift ;;
        --team) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; team="$2"; shift 2 ;;
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
signing=()
destination='generic/platform=macOS'
if [[ -n "$team" ]]; then
    [[ "$team" =~ ^[A-Z0-9]{10}$ ]] || { printf 'Use the 10-character Apple Developer team ID.\n' >&2; exit 2; }
    signing=(DEVELOPMENT_TEAM="$team" CODE_SIGN_STYLE=Automatic CODE_SIGN_IDENTITY="Apple Development"
        CALLAYERS_APP_ENTITLEMENTS=Config/App.entitlements -allowProvisioningUpdates -allowProvisioningDeviceRegistration)
    # A concrete Mac destination lets Xcode register this development device.
    if [[ "$arch" == "$host_arch" ]]; then destination="platform=macOS,arch=$arch"; fi
fi
command -v python3 >/dev/null || { printf 'Python 3 is required to acquire the renderer SDK.\n' >&2; exit 1; }
sdk="$(python3 "$root/scripts/acquire-macos-renderer.py" --arch "$arch")"
build="$root/build/macos-$arch"
if [[ -n "$team" ]]; then build="$root/build/macos-camera-$arch"; fi
mkdir -p "$build"
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/App/Annotations.swift" "$root/macos/App/WorkspaceModels.swift" "$root/macos/App/WorkspaceStore.swift" \
    "$root/macos/Tests/WorkspaceChecks.swift" -o "$build/WorkspaceChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/WorkspaceChecks" 2>&1 | tee "$build/workspace-checks.log"
fi
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/CameraExtension/TestPicture.swift" "$root/macos/Tests/CameraChecks.swift" -o "$build/CameraChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/CameraChecks" "$build/camera-test-picture.png" 2>&1 | tee "$build/camera-checks.log"
fi
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/Video/FrameComposer.swift" "$root/macos/Video/LatestVideoFrame.swift" \
    "$root/macos/Tests/VideoChecks.swift" -o "$build/VideoChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/VideoChecks" 2>&1 | tee "$build/video-checks.log"
fi
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/App/Annotations.swift" "$root/macos/App/WorkspaceModels.swift" "$root/macos/App/WorkspaceStore.swift" \
    "$root/macos/App/ImageImport.swift" "$root/macos/Tests/ImageImportChecks.swift" -o "$build/ImageImportChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/ImageImportChecks" "$build/imported-card.html" 2>&1 | tee "$build/image-checks.log"
fi
xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library -target "$arch-apple-macos12.3" \
    "$root/macos/App/Annotations.swift" "$root/macos/App/AnnotationRenderer.swift" \
    "$root/macos/App/WorkspaceModels.swift" "$root/macos/App/WorkspaceStore.swift" \
    "$root/macos/Tests/AnnotationChecks.swift" -o "$build/AnnotationChecks"
if [[ "$arch" == "$host_arch" ]]; then
    "$build/AnnotationChecks" 2>&1 | tee "$build/annotation-checks.log"
fi
xcodebuild -project "$root/macos/Callayers.xcodeproj" \
    -scheme Callayers -configuration Debug -destination "$destination" \
    -derivedDataPath "$build" ARCHS="$arch" ONLY_ACTIVE_ARCH=YES \
    ULTRALIGHT_ROOT="$sdk" ${signing[@]+"${signing[@]}"} build 2>&1 | tee "$build/build.log"
products="$build/Build/Products/Debug"
app="$products/Callayers Renderer Preview.app"
/usr/bin/codesign --verify --deep --strict "$app"
/usr/bin/codesign --verify --strict "$app/Contents/Library/SystemExtensions/com.callayers.renderer-preview.camera.systemextension"
if [[ "$arch" == "$host_arch" ]]; then
    python3 "$root/macos/Tests/run-renderer-checks.py" \
        "$products/RendererChecks" "$app/Contents/Resources" "$build/renderer-preview.png" "$build/imported-card.html" \
        2>&1 | tee "$build/renderer-checks.log"
else
    printf 'Built %s; renderer execution checks require a %s Mac.\n' "$arch" "$arch"
fi
printf '\nMac preview: %s\n' "$app"
if [[ -n "$team" ]]; then
    printf 'To activate the camera, install this signed app in /Applications, open it there, and choose Camera setup.\n'
fi
if [[ "$launch" == 1 ]]; then open "$app"; fi
