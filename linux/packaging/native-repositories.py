#!/usr/bin/env python3
"""Sign native packages and construct standard DNF and pacman repositories."""
import os
from pathlib import Path
import shutil
import subprocess

def build_native(native, output, version, fingerprint):
    rpm = native / f"callayers-{version}-1.x86_64.rpm"
    arch = native / f"callayers-{version}-1-x86_64.pkg.tar.zst"
    if not rpm.is_file() or not arch.is_file():
        raise ValueError("Both native packages must match the baseline version")
    # repo-add hides bsdtar failures behind a misleading "not a package file".
    # Check the tool and archive before signing or creating repository metadata.
    if shutil.which("bsdtar") is None:
        raise RuntimeError("Arch repository creation requires bsdtar; install libarchive-tools")
    subprocess.run(["bsdtar", "-tqf", str(arch), ".PKGINFO"], check=True, stdout=subprocess.DEVNULL)
    def run(*args):
        subprocess.run(list(map(str, args)), check=True)
    def sign(file, armor=False):
        run("gpg", "--batch", "--yes", "--digest-algo", "SHA256", "--local-user", fingerprint,
            *( ["--armor"] if armor else [] ), "--detach-sign", file)
    rpm_dir = output / "linux/rpm"
    packages = rpm_dir / "packages"
    packages.mkdir(parents=True)
    signed_rpm = packages / rpm.name
    shutil.copy2(rpm, signed_rpm)
    run("rpmsign", "--define", f"_gpg_name {fingerprint}", "--define", f"_gpg_path {os.environ.get('GNUPGHOME', '')}",
        "--define", "__gpg /usr/bin/gpg", "--addsign", signed_rpm)
    run("createrepo_c", "--no-database", "--unique-md-filenames", "--general-compress-type", "gz", "--checksum", "sha256", "--revision", version, rpm_dir)
    sign(rpm_dir / "repodata/repomd.xml", armor=True)
    arch_dir = output / "linux/arch/x86_64"
    arch_dir.mkdir(parents=True)
    signed_arch = arch_dir / arch.name
    shutil.copy2(arch, signed_arch)
    sign(signed_arch)
    # repo-add 6.x embeds the adjacent package signature automatically.
    run("repo-add", "--sign", "--key", fingerprint, arch_dir / "callayers.db.tar.gz", signed_arch)
    public = subprocess.check_output(["gpg", "--batch", "--armor", "--export", fingerprint])
    (output / "linux/callayers-archive-key.asc").write_bytes(public)
