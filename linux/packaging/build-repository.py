#!/usr/bin/env python3
"""Produce a signed, static APT repository for the Ubuntu LTS beta."""
import argparse
from datetime import datetime, timezone
from email.utils import format_datetime
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess

def build(package, output, fingerprint, native=None):
    if not re.fullmatch(r"[A-Fa-f0-9]{40}", fingerprint):
        raise ValueError("An explicit signing-key fingerprint is required")
    def field(name):
        return subprocess.check_output(["dpkg-deb", "-f", str(package), name], text=True).strip()
    version = field("Version")
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})", version):
        raise ValueError("Invalid package version")
    if field("Package") != "callayers" or field("Architecture") != "amd64":
        raise ValueError("Expected Callayers amd64 package")
    output.mkdir(parents=True, exist_ok=False)
    if native:
        import runpy
        runpy.run_path(str(Path(__file__).with_name("native-repositories.py")))["build_native"](native, output, version, fingerprint)
    def write(key, data):
        target = output / key
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    digest = lambda data: hashlib.sha256(data).hexdigest()
    filename = f"pool/main/c/callayers/callayers_{version}_amd64.deb"
    payload = package.read_bytes()
    write("linux/apt/" + filename, payload)
    control = subprocess.check_output(["dpkg-deb", "-f", str(package)], text=True).rstrip()
    index = f"{control}\nFilename: {filename}\nSize: {len(payload)}\nSHA256: {digest(payload)}\n\n".encode()
    indices = {"main/binary-amd64/Packages": index, "main/binary-amd64/Packages.gz": gzip.compress(index, mtime=0)}
    release = ("Origin: Callayers\nLabel: Callayers Ubuntu beta\nSuite: stable\nCodename: stable\n"
               "Architectures: amd64\nComponents: main\nAcquire-By-Hash: yes\n"
               f"Date: {format_datetime(datetime.now(timezone.utc), usegmt=True)}\n"
               f"X-Callayers-Version: {version}\nX-Callayers-Ubuntu: 22.04 24.04 26.04\n"
               "Description: Callayers for Ubuntu LTS x64\n" +
               ("X-Callayers-Native: fedora arch\nX-Callayers-Fedora: 43 44\n" if native else "") + "SHA256:\n")
    for name, data in indices.items():
        write("linux/apt/dists/stable/" + name, data)
        write("linux/apt/dists/stable/main/binary-amd64/by-hash/SHA256/" + digest(data), data)
        release += f" {digest(data)} {len(data)} {name}\n"
    release_file = output / "linux/apt/dists/stable/Release"
    write("linux/apt/dists/stable/Release", release.encode())
    for option, name in (("--clearsign", "InRelease"), ("--detach-sign", "Release.gpg")):
        subprocess.run(["gpg", "--batch", "--yes", "--digest-algo", "SHA256", "--local-user", fingerprint,
                        "--output", str(release_file.with_name(name)), option, str(release_file)], check=True)
    public = subprocess.check_output(["gpg", "--batch", "--export", fingerprint])
    if not public:
        raise ValueError("Signing public key is unavailable")
    write("linux/callayers-archive-keyring.gpg", public)
    write("linux/install.sh", Path(__file__).with_name("install.sh").read_bytes())
    inventory = [{"key": file.relative_to(output).as_posix(), "sha256": digest(file.read_bytes()), "size": file.stat().st_size}
                 for file in sorted(output.rglob("*")) if file.is_file()]
    (output / "manifest.json").write_text(json.dumps({"version": version, "files": inventory}, indent=2) + "\n")
    print(f"Signed Ubuntu beta repository prepared: {output}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fingerprint", required=True)
    parser.add_argument("--native", type=Path)
    args = parser.parse_args()
    build(args.package.resolve(), args.output.resolve(), args.fingerprint, args.native.resolve() if args.native else None)
