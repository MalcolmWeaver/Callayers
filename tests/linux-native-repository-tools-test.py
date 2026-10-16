"""Exercise repo-add against the actual release artifact before importing keys."""
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tarfile
import tempfile
from unittest.mock import patch

package = Path(sys.argv[1]).resolve()
match = re.fullmatch(r"callayers-(\d+\.\d+\.\d+)-1-x86_64\.pkg\.tar\.zst", package.name)
assert match, "Expected a versioned Callayers Arch package"
version = match[1]
builder = runpy.run_path(str(Path(__file__).resolve().parents[1] / "linux/packaging/native-repositories.py"))["build_native"]

with tempfile.TemporaryDirectory(prefix="callayers-repo-tools-") as temporary:
    root = Path(temporary)
    # Model the signing runner's original missing executable. Fail before any
    # signatures or output, with the package name needed to repair the runner.
    with patch.dict(os.environ, {"PATH": str(root)}):
        try:
            builder(package.parent, root / "missing-tool", version, "0" * 40)
        except RuntimeError as error:
            assert "install libarchive-tools" in str(error), str(error)
        else:
            raise AssertionError("Missing bsdtar did not stop repository creation")
    assert not (root / "missing-tool").exists()

    # Unlike pacman -Qip, repo-add shells out to bsdtar. Run that exact path
    # against the compressed release package with the installed CI toolchain.
    metadata = subprocess.check_output(["bsdtar", "-xOf", str(package), ".PKGINFO"], text=True)
    assert f"pkgver = {version}-1\n" in metadata
    database = root / "callayers.db.tar.gz"
    subprocess.run(["repo-add", str(database), str(package)], check=True)
    with tarfile.open(database, "r:gz") as archive:
        descriptions = [entry for entry in archive.getmembers() if entry.name.endswith("/desc")]
        assert len(descriptions) == 1
        description = archive.extractfile(descriptions[0]).read().decode()
    assert f"%VERSION%\n{version}-1\n" in description
    assert f"%FILENAME%\n{package.name}\n" in description

print("Missing-tool diagnostic and real Arch repository creation passed")
