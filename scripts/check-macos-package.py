#!/usr/bin/env python3
"""Validate a self-contained architecture-specific Callayers app without launching it."""
import argparse
import base64
import pathlib
import plistlib
import re
import subprocess


def run(*args):
    result = subprocess.run(args, check=True, capture_output=True, text=True)
    return result.stdout + result.stderr


def verify(app, arch, distribution=False, team=None):
    app = app.resolve(strict=True)
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    assert info['CFBundleIdentifier'] == 'com.callayers.renderer-preview', 'Unexpected host identity'
    assert re.fullmatch(r'\d+\.\d+\.\d+', info['CFBundleShortVersionString']), 'Invalid release version'
    assert re.fullmatch(r'[1-9]\d*', info['CFBundleVersion']), 'Invalid build number'
    extension = app / 'Contents/Library/SystemExtensions/com.callayers.renderer-preview.camera.systemextension'
    ext_info = plistlib.loads((extension / 'Contents/Info.plist').read_bytes())
    assert ext_info['CFBundleIdentifier'] == 'com.callayers.renderer-preview.camera', 'Unexpected extension identity'
    for path in [app / 'Contents/MacOS' / info['CFBundleExecutable'], extension / 'Contents/MacOS' / ext_info['CFBundleExecutable']] + [app / f'Contents/Frameworks/lib{name}.dylib' for name in ['AppCore', 'Ultralight', 'UltralightCore', 'WebCore']]:
        assert run('/usr/bin/lipo', '-archs', str(path)).strip() == arch, f'Wrong architecture: {path.name}'
    assert (app / 'Contents/Frameworks/Sparkle.framework').is_dir(), 'Updater framework missing'
    for resource in ['resources/icudt67l.dat', 'resources/cacert.pem', 'RendererLicenses/VERSION.txt', 'Sparkle-LICENSE.txt', 'scene-host.html', 'scene-host.js', 'card-bridge.js']:
        assert (app / 'Contents/Resources' / resource).is_file(), f'Missing resource: {resource}'
    for path in app.rglob('*'):
        if path.is_symlink():
            assert path.resolve().is_relative_to(app), 'A bundle symlink escapes the app'
            continue
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf'):
            continue
        assert arch in run('/usr/bin/lipo', '-archs', str(path)).split(), f'Incompatible executable: {path.name}'
        links = run('/usr/bin/otool', '-arch', arch, '-L', str(path))
        for line in links.splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith('/'):
                assert dependency.startswith(('/usr/lib/', '/System/Library/')), f'External build dependency: {dependency}'
        run('/usr/bin/codesign', '--verify', '--strict', str(path))
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(app))
    if distribution:
        assert info.get('CallayersRelease') == 'YES', 'Development build is not distributable'
        assert len(base64.b64decode(info.get('SUPublicEDKey', ''), validate=True)) == 32, 'Missing update public key'
        assert info.get('SUFeedURL') == f'https://downloads.callayers.com/macos/{arch}/appcast.xml', 'Wrong architecture update feed'
        assert info.get('SURequireSignedFeed') is True and info.get('SUVerifyUpdateBeforeExtraction') is True, 'Update authenticity checks missing'
        for target in [app, extension]:
            details = run('/usr/bin/codesign', '-d', '--verbose=4', str(target))
            assert 'Authority=Developer ID Application:' in details and '(runtime)' in details, 'Developer ID/hardened runtime missing'
            if team:
                assert f'TeamIdentifier={team}' in details, 'Wrong signing team'
            output = subprocess.run(['/usr/bin/codesign', '-d', '--entitlements', ':-', str(target)], check=True, capture_output=True).stdout
            entitlements = plistlib.loads(output) if output else {}
            assert not entitlements.get('com.apple.security.get-task-allow'), 'Debug entitlement in release'
            assert not entitlements.get('com.apple.security.cs.disable-library-validation'), 'Library validation disabled'
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=pathlib.Path)
    parser.add_argument('--arch', required=True, choices=['arm64', 'x86_64'])
    parser.add_argument('--distribution', action='store_true')
    parser.add_argument('--team')
    args = parser.parse_args()
    info = verify(args.app, args.arch, args.distribution, args.team)
    print(f"PASS {args.arch} bundle {info['CFBundleShortVersionString']} ({info['CFBundleVersion']}): architectures, dependencies, resources and signatures")


if __name__ == '__main__':
    main()
