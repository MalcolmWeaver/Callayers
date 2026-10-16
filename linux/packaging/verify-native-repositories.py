#!/usr/bin/env python3
"""Verify native signatures and index/package hashes without changing the host."""
import base64
import gzip
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET

def verify(root, version):
    if not re.fullmatch(r"(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})", version):
        raise ValueError("Invalid native release version")
    rpm_root, arch_root = root / "linux/rpm", root / "linux/arch/x86_64"
    rpm = rpm_root / f"packages/callayers-{version}-1.x86_64.rpm"
    arch = arch_root / f"callayers-{version}-1-x86_64.pkg.tar.zst"
    key = root / "linux/callayers-archive-keyring.gpg"
    with tempfile.TemporaryDirectory(prefix="callayers-native-verify-") as temporary:
        home = Path(temporary)
        def run(*args):
            subprocess.run(list(map(str, args)), check=True, stdout=subprocess.DEVNULL)
        def signature(file, suffix=".sig"):
            run("gpgv", "--homedir", home, "--keyring", key, str(file) + suffix, file)
        signature(rpm_root / "repodata/repomd.xml", ".asc")
        signature(arch)
        for name in ['callayers.db', 'callayers.files']:
            canonical = arch_root / name
            signature(canonical)
            assert canonical.read_bytes() == Path(str(canonical) + '.tar.gz').read_bytes()
            assert Path(str(canonical) + '.sig').read_bytes() == Path(str(canonical) + '.tar.gz.sig').read_bytes()
        # The ASCII key used by DNF must be the exact same signing identity.
        def fingerprint(file):
            text = subprocess.check_output(["gpg", "--homedir", str(home), "--show-keys", "--with-colons", str(file)], text=True)
            return next(line.split(':')[9] for line in text.splitlines() if line.startswith('fpr:'))
        ascii_key = root / "linux/callayers-archive-key.asc"
        assert fingerprint(key) == fingerprint(ascii_key), "Native signing key mismatch"
        rpm_db = home / "rpm"
        rpm_db.mkdir()
        run("rpmkeys", "--dbpath", rpm_db, "--import", ascii_key)
        run("rpmkeys", "--dbpath", rpm_db, "--checksig", rpm)
        repo_ns = {'r': 'http://linux.duke.edu/metadata/repo'}
        repomd = ET.parse(rpm_root / "repodata/repomd.xml")
        primary = None
        for entry in repomd.findall('r:data', repo_ns):
            location = entry.find('r:location', repo_ns).attrib['href']
            if not re.fullmatch(r'repodata/[a-f0-9]{64}-[A-Za-z0-9._-]+', location):
                raise ValueError("Unexpected RPM metadata location")
            content = (rpm_root / location).read_bytes()
            checksum = entry.find('r:checksum', repo_ns)
            assert checksum.attrib['type'] == 'sha256' and hashlib.sha256(content).hexdigest() == checksum.text
            if entry.attrib['type'] == 'primary':
                primary = ET.fromstring(gzip.decompress(content))
        assert primary is not None
        common = {'c': 'http://linux.duke.edu/metadata/common'}
        packages = primary.findall('c:package', common)
        assert len(packages) == 1
        entry = packages[0]
        assert entry.find('c:name', common).text == 'callayers'
        assert entry.find('c:version', common).attrib['ver'] == version
        assert entry.find('c:location', common).attrib['href'] == f'packages/{rpm.name}'
        assert entry.find('c:checksum', common).text == hashlib.sha256(rpm.read_bytes()).hexdigest()
        with tarfile.open(arch_root / 'callayers.db', 'r:gz') as database:
            descriptions = [member for member in database.getmembers() if member.name.endswith('/desc')]
            assert len(descriptions) == 1
            description = database.extractfile(descriptions[0]).read().decode()
        def field(name):
            return re.search(rf'%{name}%\n([^\n]+)', description)[1]
        assert field('NAME') == 'callayers' and field('VERSION') == version + '-1'
        assert field('FILENAME') == arch.name
        assert field('SHA256SUM') == hashlib.sha256(arch.read_bytes()).hexdigest()
        assert base64.b64decode(field('PGPSIG')) == Path(str(arch) + '.sig').read_bytes()
    print('Native repository signatures, package versions, and index checksums verified')

if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve(), sys.argv[2])
