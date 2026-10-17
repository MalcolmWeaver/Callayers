#!/usr/bin/env python3
"""Exercise the actual pinned Sparkle signer with a disposable fixture key."""
import hashlib
import pathlib
import plistlib
import xml.etree.ElementTree as ET
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
signer = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='callayers-update-fixture-') as temporary:
    folder = pathlib.Path(temporary)
    key = folder / 'fixture.key'
    command = ['xcrun', 'swift', str(ROOT / 'scripts/macos-update-key.swift')]
    public = subprocess.check_output(command + ['--generate', str(key)], text=True).strip()
    assert len(public) == 44 and key.stat().st_mode & 0o777 == 0o600
    before = hashlib.sha256(key.read_bytes()).digest()
    retry = subprocess.run(command + ['--generate', str(key)], capture_output=True)
    assert retry.returncode != 0 and hashlib.sha256(key.read_bytes()).digest() == before, 'Existing key was replaced'
    assert subprocess.check_output(command + ['--public', str(key)], text=True).strip() == public
    archive = folder / 'fixture.zip'
    archive.write_bytes(b'disposable update payload')
    signature = subprocess.check_output([str(signer), '--ed-key-file', str(key), '-p', str(archive)], text=True).strip()
    subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(archive), signature], check=True, capture_output=True)
    archive.write_bytes(b'altered update payload')
    rejected = subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(archive), signature], capture_output=True)
    assert rejected.returncode != 0, 'Tampered update accepted'
    feed = folder / 'appcast.xml'
    feed.write_text('<?xml version="1.0"?><rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><title>Fixture</title></channel></rss>')
    subprocess.run([str(signer), '--ed-key-file', str(key), str(feed)], check=True, capture_output=True)
    subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(feed)], check=True, capture_output=True)
    feed.write_text(feed.read_text().replace('Fixture', 'Tampered'))
    rejected = subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(feed)], capture_output=True)
    assert rejected.returncode != 0, 'Tampered appcast accepted'
    if len(sys.argv) > 2:
        # Exercise the complete archive-to-feed tool with a disposable signed copy.
        # The app is never launched or installed, and the development bundle is unchanged.
        source = pathlib.Path(sys.argv[2]).resolve()
        app = folder / 'payload/Callayers.app'
        subprocess.run(['/usr/bin/ditto', str(source), str(app)], check=True)
        info_path = app / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes()); info['SUPublicEDKey'] = public
        info_path.write_bytes(plistlib.dumps(info))
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(app)], check=True, capture_output=True)
        updates = folder / 'updates'; updates.mkdir()
        update = updates / 'Callayers-fixture.zip'
        subprocess.run(['/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(update)], check=True)
        subprocess.run([str(signer.parent / 'generate_appcast'), '--ed-key-file', str(key), '--maximum-deltas', '0',
                        '--download-url-prefix', 'https://example.invalid/updates/', str(updates)], check=True, capture_output=True)
        appcast = updates / 'appcast.xml'
        subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(appcast)], check=True, capture_output=True)
        enclosure = ET.parse(appcast).find('./channel/item/enclosure')
        assert enclosure is not None and enclosure.attrib['url'] == 'https://example.invalid/updates/Callayers-fixture.zip'
        signature = enclosure.attrib['{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature']
        subprocess.run([str(signer), '--verify', '--ed-key-file', str(key), str(update), signature], check=True, capture_output=True)
        assert int(enclosure.attrib['length']) == update.stat().st_size
        print('PASS complete app archive generates an authenticated feed and matching download signature')
print('PASS actual Sparkle archive/feed signatures, tamper rejection and non-overwriting private key creation')
