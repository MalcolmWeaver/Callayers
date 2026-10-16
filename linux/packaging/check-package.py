#!/usr/bin/env python3
"""Check the extracted production package without installing it or opening a camera."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

package = Path(sys.argv[1]).resolve()
def field(name):
    return subprocess.check_output(["dpkg-deb", "-f", str(package), name], text=True).strip()

assert field("Package") == "callayers" and field("Architecture") == "amd64"
for dependency in ("v4l2loopback-dkms", "qt6-qpa-plugins", "qt6-wayland"):
    assert dependency in field("Depends"), f"Missing runtime dependency: {dependency}"
with tempfile.TemporaryDirectory(prefix="callayers-package-") as directory:
    root = Path(directory)
    subprocess.run(["dpkg-deb", "-x", str(package), directory], check=True)
    executable = root / "usr/bin/callayers"
    for relative in ("usr/share/callayers/web/overlay.html", "usr/share/callayers/resources/icudt67l.dat",
                     "usr/lib/callayers/libUltralight.so", "usr/share/applications/callayers.desktop",
                     "etc/modprobe.d/callayers.conf", "etc/modules-load.d/callayers.conf"):
        assert (root / relative).is_file(), f"Missing package resource: {relative}"
    dynamic = subprocess.check_output(["readelf", "-d", str(executable)], text=True)
    assert "$ORIGIN/../lib/callayers" in dynamic
    assert "/.deps/" not in dynamic and "/build/" not in dynamic, "Development library path in package"
    env = {key: value for key, value in os.environ.items() if key not in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH")}
    env.update(QT_QPA_PLATFORM="offscreen", XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
    subprocess.run([str(executable), "--self-check"], env=env, cwd=directory, check=True, timeout=20)
    for flag in ("--skip-sign-in", "--offline-preview"):
        result = subprocess.run([str(executable), flag], env=env, cwd=directory, timeout=10)
        assert result.returncode == 2, f"Production did not reject {flag}"
print(f"Production package {field('Version')}: bundled renderer loads; development flags rejected")
