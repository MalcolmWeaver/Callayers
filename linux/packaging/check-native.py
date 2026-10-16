#!/usr/bin/env python3
"""Validate native metadata, packaged resources, and the production renderer."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

def check(package, kind):
    package = package.resolve()
    if kind == "rpm":
        fields = subprocess.check_output(["rpm", "-qp", "--queryformat", "%{NAME} %{ARCH}\n", str(package)], text=True).strip()
        assert fields == "callayers x86_64", fields
        dependencies = subprocess.check_output(["rpm", "-qp", "--requires", str(package)], text=True)
        assert "akmod-v4l2loopback" in dependencies and "qt6-qtbase" in dependencies
        hooks = subprocess.check_output(["rpm", "-qp", "--scripts", str(package)], text=True)
        assert "/usr/lib/callayers/setup-camera.sh" in hooks
    else:
        info = subprocess.check_output(["bsdtar", "-xOf", str(package), ".PKGINFO"], text=True)
        for value in ["pkgname = callayers\n", "arch = x86_64\n", "depend = v4l2loopback-dkms\n", "depend = qt6-base>=6.2\n"]:
            assert value in info, value
        hooks = subprocess.check_output(["bsdtar", "-xOf", str(package), ".INSTALL"], text=True)
        assert "post_install" in hooks and "/usr/lib/callayers/setup-camera.sh" in hooks
        subprocess.run(["bsdtar", "-xOf", str(package), ".MTREE"], check=True, stdout=subprocess.DEVNULL)
        # Ask the actual package manager to parse the archive too.
        subprocess.run(["pacman", "-Qip", str(package)], check=True, stdout=subprocess.DEVNULL)
        # An empty alternate root has no kernel or system hooks. Check installed
        # file metadata there without executing any package scripts or touching
        # the test container's own package database/configuration.
        with tempfile.TemporaryDirectory(prefix="callayers-pacman-root-") as installed:
            target = Path(installed)
            (target / 'var/lib/pacman').mkdir(parents=True)
            (target / 'cache').mkdir()
            config = target / 'pacman.conf'
            config.write_text('[options]\nArchitecture = x86_64\nSigLevel = Required DatabaseRequired\nLocalFileSigLevel = Optional\n')
            common = ['pacman', '--config', str(config), '--root', installed,
                      '--dbpath', str(target / 'var/lib/pacman'), '--cachedir', str(target / 'cache'), '--logfile', str(target / 'pacman.log')]
            # pacman requires the option twice to omit dependency presence as
            # well as versions; the real dependency check runs separately.
            subprocess.run([*common, '-U', '--nodeps', '--nodeps', '--noscriptlet', '--noconfirm', str(package)], check=True, stdout=subprocess.DEVNULL)
            subprocess.run([*common, '-Qkk', 'callayers'], check=True)
    with tempfile.TemporaryDirectory(prefix="callayers-native-check-") as directory:
        root = Path(directory)
        subprocess.run(["bsdtar", "-xf", str(package), "-C", directory], check=True)
        for file in ["usr/bin/callayers", "usr/lib/callayers/setup-camera.sh", "usr/lib/callayers/libUltralight.so",
                     "usr/share/callayers/resources/icudt67l.dat", "usr/share/callayers/web/overlay.html",
                     "usr/share/applications/callayers.desktop", "etc/modprobe.d/callayers.conf", "etc/modules-load.d/callayers.conf"]:
            assert (root / file).is_file(), file
        executable = root / "usr/bin/callayers"
        dynamic = subprocess.check_output(["readelf", "-d", str(executable)], text=True)
        assert "$ORIGIN/../lib/callayers" in dynamic and "/.deps/" not in dynamic
        env = {key: value for key, value in os.environ.items() if key not in
               ["LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "XDG_RUNTIME_DIR"]}
        env.update(QT_QPA_PLATFORM="offscreen", XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
        subprocess.run([str(executable), "--self-check"], env=env, cwd=directory, check=True, timeout=30)
        for flag in ["--skip-sign-in", "--offline-preview"]:
            result = subprocess.run([str(executable), flag], env=env, cwd=directory, timeout=10)
            assert result.returncode == 2, f"Production accepted {flag}"
    print(f"{kind} package: metadata/resources/renderer verified; development flags rejected")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--format", choices=["rpm", "arch"], required=True)
    args = parser.parse_args()
    check(args.package, args.format)
