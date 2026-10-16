#!/usr/bin/env python3
"""Wrap the tested baseline payload in native RPM/Arch packages; never rebuild it."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import time

def run(*args, **options):
    return subprocess.check_output(list(map(str, args)), **options)

def build(package, output, kind):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="callayers-native-") as temporary:
        root = Path(temporary)
        run("bsdtar", "-xf", package, "-C", root)
        data = list(root.glob("data.tar.*"))
        control = list(root.glob("control.tar.*"))
        if len(data) != 1 or len(control) != 1:
            raise ValueError("Expected one Debian payload and control archive")
        payload = root / "payload"
        payload.mkdir()
        run("bsdtar", "-xf", data[0], "-C", payload)
        metadata = run("bsdtar", "-xOf", control[0], "./control").decode()
        def field(name):
            return re.search(rf"^{name}: (.+)$", metadata, re.M)[1]
        version = field("Version")
        if (field("Package") != "callayers" or field("Architecture") != "amd64" or
                not re.fullmatch(r"(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})", version)):
            raise ValueError("Expected a versioned Callayers amd64 baseline")
        if not (payload / "usr/lib/callayers/setup-camera.sh").is_file():
            raise ValueError("Baseline package is missing the shared camera hook")
        # Debian build umasks can leave shared parent directories group-writable;
        # native packages must agree with Fedora/Arch's standard 0755 parents.
        for item in [payload, *payload.rglob('*')]:
            if item.is_symlink():
                continue
            item.chmod(0o755 if item.is_dir() or item.stat().st_mode & 0o111 else 0o644)
        if kind == "rpm":
            spec = root / "callayers.spec"
            spec.write_text(f"""%global __os_install_post %{{nil}}
%global _build_id_links none
Name: callayers
Version: {version}
Release: 1
Summary: Webcam cards and annotations for video calls
License: LicenseRef-Proprietary
URL: https://callayers.com
BuildArch: x86_64
AutoReqProv: no
Requires: glibc >= 2.35, libstdc++, libgcc, qt6-qtbase >= 6.2, qt6-qtbase-gui, qt6-qtwayland, fontconfig, libX11, libxcb, dejavu-sans-fonts
Requires: akmod-v4l2loopback
%description
Callayers combines your webcam, cards, and annotations into a virtual camera.
%prep
%build
%install
mkdir -p %{{buildroot}}
cp -a {payload}/. %{{buildroot}}/
%post
/usr/lib/callayers/setup-camera.sh
%files
%defattr(-,root,root,-)
/usr/bin/callayers
/usr/lib/callayers
/usr/share/callayers
/usr/share/doc/callayers
/usr/share/applications/callayers.desktop
/usr/share/icons/hicolor/scalable/apps/callayers.svg
%config(noreplace) /etc/modprobe.d/callayers.conf
%config(noreplace) /etc/modules-load.d/callayers.conf
""")
            run("rpmbuild", "-bb", "--define", f"_topdir {root}/rpmbuild", "--define", "_buildhost callayers", "--define", "_rpmformat 4", spec)
            built = next((root / "rpmbuild/RPMS/x86_64").glob("callayers-*.rpm"))
            target = output / built.name
            shutil.copy2(built, target)
        else:
            # pacman accepts the documented .PKGINFO + payload archive format.
            # No makepkg/root bypass or compiler is involved: the payload was
            # already built and checked in the Ubuntu baseline job.
            target = output / f"callayers-{version}-1-x86_64.pkg.tar.zst"
            dependencies = ["glibc>=2.35", "gcc-libs", "qt6-base>=6.2", "qt6-wayland", "fontconfig", "libx11", "libxcb", "ttf-dejavu", "v4l2loopback-dkms"]
            info = (f"pkgname = callayers\npkgbase = callayers\npkgver = {version}-1\npkgdesc = Webcam cards and annotations\n"
                    f"url = https://callayers.com\nbuilddate = {int(time.time())}\npackager = Callayers\n"
                    f"size = {sum(p.stat().st_size for p in payload.rglob('*') if p.is_file())}\narch = x86_64\nlicense = custom\n"
                    "backup = etc/modprobe.d/callayers.conf\nbackup = etc/modules-load.d/callayers.conf\n" +
                    "".join(f"depend = {value}\n" for value in dependencies)).encode()
            hook = b'post_install() { /usr/lib/callayers/setup-camera.sh; }\npost_upgrade() { post_install; }\n'
            (payload / ".PKGINFO").write_bytes(info)
            (payload / ".INSTALL").write_bytes(hook)
            (payload / ".PKGINFO").chmod(0o644)
            (payload / ".INSTALL").chmod(0o644)
            # Include pacman's standard integrity inventory, with package-owned
            # UID/GID metadata rather than the builder's user identity.
            mtree = run("bsdtar", "--format=mtree", "--options=!all,use-set,type,uid,gid,mode,time,size,md5,sha256,link",
                        "--uid", "0", "--gid", "0", "-czf", "-", "-C", payload, ".")
            (payload / ".MTREE").write_bytes(mtree)
            (payload / ".MTREE").chmod(0o644)
            archive = root / "arch.tar"
            with tarfile.open(archive, "w", format=tarfile.PAX_FORMAT) as stream:
                def ownership(entry):
                    entry.uid = entry.gid = 0
                    entry.uname = entry.gname = "root"
                    return entry
                for directory in sorted(payload.iterdir()):
                    stream.add(directory, arcname=directory.name, filter=ownership)
            run("zstd", "-q", "-f", archive, "-o", target)
        print(target)
        return target

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--format", required=True, choices=["rpm", "arch"])
    args = parser.parse_args()
    build(args.package.resolve(), args.output.resolve(), args.format)
