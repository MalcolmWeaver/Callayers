#!/bin/bash
# Xcode build phase: give the preview a self-contained SDK and resource bundle.
set -euo pipefail
# Architecture-conditional Xcode settings may be empty during a script phase.
# The command-line build supplies an explicit root; a direct Xcode Run can
# resolve the already-acquired SDK from its single selected architecture.
if [[ -z "${ULTRALIGHT_ROOT:-}" ]]; then
    case "${ARCHS:-}" in
        arm64) ULTRALIGHT_ROOT="$SRCROOT/../.deps/ultralight-macos-arm64" ;;
        x86_64) ULTRALIGHT_ROOT="$SRCROOT/../.deps/ultralight-macos-x64" ;;
        *) printf 'Select a single native architecture for this preview.\n' >&2; exit 1 ;;
    esac
fi
: "${TARGET_BUILD_DIR:?Run this script through Xcode}"
: "${FRAMEWORKS_FOLDER_PATH:?Missing app framework directory}"
: "${UNLOCALIZED_RESOURCES_FOLDER_PATH:?Missing app resource directory}"
frameworks="$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH"
resources="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"
mkdir -p "$frameworks" "$resources/resources" "$resources/RendererLicenses"
for library in AppCore Ultralight UltralightCore WebCore; do
    source_library="$ULTRALIGHT_ROOT/bin/lib$library.dylib"
    /usr/bin/ditto "$source_library" "$frameworks/lib$library.dylib"
    # Use the app's identity for team-signed builds as well as local previews.
    /usr/bin/codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY:--}" --timestamp=none "$frameworks/lib$library.dylib"
done
for resource in icudt67l.dat cacert.pem; do
    /usr/bin/ditto "$ULTRALIGHT_ROOT/resources/$resource" "$resources/resources/$resource"
done
/usr/bin/ditto "$ULTRALIGHT_ROOT/license" "$resources/RendererLicenses"
/usr/bin/ditto "$ULTRALIGHT_ROOT/VERSION.txt" "$resources/RendererLicenses/VERSION.txt"
/usr/bin/ditto "$SRCROOT/../spec-v2/assets/callayers.png" "$resources/callayers.png"
