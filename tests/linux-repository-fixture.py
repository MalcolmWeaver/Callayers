"""Disposable test package and signing identity; never a release key."""
import os
from pathlib import Path
import subprocess
import sys

def fixture(directory):
    directory = Path(directory).resolve()
    home = directory / "gnupg"
    home.mkdir(mode=0o700)
    env = dict(os.environ, GNUPGHOME=str(home))
    subprocess.run(["gpg", "--batch", "--pinentry-mode", "loopback", "--passphrase", "", "--quick-generate-key",
                    "Callayers disposable repository test", "rsa3072", "sign", "1d"], env=env, check=True)
    keys = subprocess.check_output(["gpg", "--batch", "--with-colons", "--list-secret-keys"], env=env, text=True)
    fingerprint = next(line.split(":")[9] for line in keys.splitlines() if line.startswith("fpr:"))
    stage = directory / "package"
    (stage / "DEBIAN").mkdir(parents=True)
    (stage / "DEBIAN/control").write_text("Package: callayers\nVersion: 1.0.0\nArchitecture: amd64\nMaintainer: Test <test@example.invalid>\nDescription: Disposable repository fixture\n")
    package = directory / "callayers_1.0.0_amd64.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(stage), str(package)], check=True, capture_output=True)
    builder = Path(__file__).resolve().parents[1] / "linux/packaging/build-repository.py"
    try:
        subprocess.run([sys.executable, str(builder), str(package), str(directory / "repository"), "--fingerprint", fingerprint], env=env, check=True)
    finally:
        subprocess.run(["gpgconf", "--kill", "gpg-agent"], env=env, check=False)
    return directory / "repository"

if __name__ == "__main__":
    fixture(sys.argv[1])
