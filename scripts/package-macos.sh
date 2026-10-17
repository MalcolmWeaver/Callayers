#!/bin/bash
# Prepare a local development archive or a Developer ID/notarized release.
# This script never publishes artifacts or changes an installed application.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=""; team=""; identity=""; profile=""; key_file=""; out=""; development=0; skip_build=0
usage() { printf 'Usage: bash scripts/package-macos.sh --arch arm64|x86_64 --team TEAM [--development [--skip-build]] [--identity "Developer ID Application: …" --notary-profile PROFILE --ed-key-file FILE] [--output DIRECTORY]\n'; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--team|--identity|--notary-profile|--ed-key-file|--output)
            [[ $# -ge 2 ]] || { usage >&2; exit 2; }
            case "$1" in --arch) arch="$2";; --team) team="$2";; --identity) identity="$2";; --notary-profile) profile="$2";; --ed-key-file) key_file="$2";; --output) out="$2";; esac
            shift 2;;
        --development) development=1; shift;;
        --skip-build) skip_build=1; shift;;
        --help|-h) usage; exit 0;;
        *) usage >&2; exit 2;;
    esac
done
[[ "$arch" == arm64 || "$arch" == x86_64 ]] || { usage >&2; exit 2; }
[[ "$team" =~ ^[A-Z0-9]{10}$ ]] || { usage >&2; exit 2; }
[[ "$(uname -s)" == Darwin ]] || { printf 'Packaging requires macOS and Xcode.\n' >&2; exit 1; }
if [[ "$development" == 0 ]]; then
    [[ "$skip_build" == 0 && "$identity" == 'Developer ID Application:'* && -n "$profile" && -f "$key_file" ]] || { usage >&2; exit 2; }
    /usr/bin/security find-identity -v -p codesigning | /usr/bin/grep -F -- "$identity" >/dev/null || { printf 'The requested Developer ID Application certificate is not available.\n' >&2; exit 1; }
fi
out="${out:-$root/build/macos-packages/$arch}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
if [[ "$development" == 1 ]]; then
    if [[ "$skip_build" == 0 ]]; then bash "$root/scripts/build-macos.sh" --arch "$arch" --team "$team"; fi
    app="$root/build/macos-camera-$arch/Build/Products/Debug/Callayers.app"
    suffix="-development"
else
    public_key="$(xcrun swift "$root/scripts/macos-update-key.swift" --public "$key_file")"
    sdk="$(python3 "$root/scripts/acquire-macos-renderer.py" --arch "$arch")"
    archive="$out/Callayers.xcarchive"
    [[ ! -e "$archive" && ! -e "$out/export" ]] || { printf 'Use a fresh output directory for each release build.\n' >&2; exit 1; }
    # Automatic archive signing uses development credentials; developer-id export
    # below re-signs the finished bundle for distribution.
    xcodebuild -project "$root/macos/Callayers.xcodeproj" -scheme Callayers -configuration Release \
        -destination 'generic/platform=macOS' -archivePath "$archive" -derivedDataPath "$out/DerivedData" \
        ARCHS="$arch" ONLY_ACTIVE_ARCH=YES ULTRALIGHT_ROOT="$sdk" CALLAYERS_UPDATE_ARCH="$arch" \
        CALLAYERS_RELEASE=YES CALLAYERS_UPDATE_PUBLIC_KEY="$public_key" ENABLE_HARDENED_RUNTIME=YES \
        DEVELOPMENT_TEAM="$team" CODE_SIGN_STYLE=Automatic CODE_SIGN_IDENTITY='Apple Development' \
        CALLAYERS_APP_ENTITLEMENTS=Config/App.entitlements -allowProvisioningUpdates archive
    python3 - "$out/export-options.plist" "$team" "$identity" <<'PY'
import plistlib,sys
with open(sys.argv[1],'wb') as stream:
    plistlib.dump({'method':'developer-id','teamID':sys.argv[2],'signingStyle':'automatic','signingCertificate':sys.argv[3],'destination':'export'},stream)
PY
    xcodebuild -exportArchive -archivePath "$archive" -exportPath "$out/export" -exportOptionsPlist "$out/export-options.plist" -allowProvisioningUpdates
    app="$out/export/Callayers.app"
    python3 "$root/scripts/check-macos-package.py" "$app" --arch "$arch" --distribution --team "$team"
    notary_zip="$out/notarization-upload.zip"
    /usr/bin/ditto -c -k --sequesterRsrc --keepParent "$app" "$notary_zip"
    xcrun notarytool submit "$notary_zip" --keychain-profile "$profile" --wait --output-format json > "$out/notarization.json"
    python3 - "$out/notarization.json" <<'PY'
import json,sys
assert json.load(open(sys.argv[1]))['status']=='Accepted', 'Notarization was not accepted; inspect the saved result.'
PY
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    /usr/sbin/spctl --assess --type execute --verbose=2 "$app"
    suffix=""
fi
python3 "$root/scripts/check-macos-package.py" "$app" --arch "$arch"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
build_number="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist")"
name="Callayers-$version-$build_number-macos-$arch$suffix.zip"
[[ ! -e "$out/$name" ]] || { printf 'Refusing to replace an existing versioned archive.\n' >&2; exit 1; }
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$app" "$out/$name"
(cd "$out" && /usr/bin/shasum -a 256 "$name" > "$name.sha256")
if [[ "$development" == 0 ]]; then
    sparkle="$out/DerivedData/SourcePackages/artifacts/sparkle/Sparkle/bin"
    feed="$out/feed"; mkdir -p "$feed"
    /usr/bin/ditto "$out/$name" "$feed/$name"
    "$sparkle/generate_appcast" --ed-key-file "$key_file" --maximum-deltas 0 \
        --download-url-prefix "https://downloads.callayers.com/macos/releases/$version/$arch/" \
        -o "$feed/appcast.xml" "$feed"
    "$sparkle/sign_update" --verify --ed-key-file "$key_file" "$feed/appcast.xml"
    python3 - "$app" "$out/$name" "$feed/latest.json" "$arch" <<'PYMAC'
import hashlib,json,plistlib,sys
from pathlib import Path
info=plistlib.loads((Path(sys.argv[1])/'Contents/Info.plist').read_bytes())
archive=Path(sys.argv[2]); arch=sys.argv[4]; version=info['CFBundleShortVersionString']
manifest={'schema':1,'platform':'macos','arch':arch,'notarized':True,'version':version,
          'build':info['CFBundleVersion'],'minimumSystemVersion':info['LSMinimumSystemVersion'],
          'url':f'https://downloads.callayers.com/macos/releases/{version}/{arch}/{archive.name}',
          'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
Path(sys.argv[3]).write_text(json.dumps(manifest,indent=2)+'\n')
PYMAC
fi
printf 'Prepared: %s\n' "$out/$name"
