"""Ask real APT to read our signed repository, and reject a tampered signature."""
import getpass
from pathlib import Path
import runpy
import subprocess
import tempfile

fixture = runpy.run_path(str(Path(__file__).with_name("linux-repository-fixture.py")))["fixture"]
with tempfile.TemporaryDirectory(prefix="callayers-apt-test-") as temporary:
    root = Path(temporary)
    repository = fixture(root)
    (root / "sources.list").write_text(f"deb [signed-by={repository}/linux/callayers-archive-keyring.gpg] file:{repository}/linux/apt stable main\n")
    (root / "state/lists/partial").mkdir(parents=True)
    (root / "cache/archives/partial").mkdir(parents=True)
    (root / "state/status").touch()
    options = []
    for setting in (f"Dir::Etc::sourcelist={root}/sources.list", "Dir::Etc::sourceparts=-", "Dir::Etc::parts=-", "Dir::Etc::main=-",
                    f"Dir::State={root}/state", f"Dir::Cache={root}/cache", "Debug::NoLocking=1",
                    f"APT::Sandbox::User={getpass.getuser()}", "APT::Update::Error-Mode=any"):
        options.extend(["-o", setting])
    result = subprocess.run(["apt-get", *options, "update"], text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    policy = subprocess.check_output(["apt-cache", *options, "policy", "callayers"], text=True)
    assert "Candidate: 1.0.0" in policy, policy
    release = repository / "linux/apt/dists/stable/InRelease"
    release.write_text(release.read_text().replace("X-Callayers-Version: 1.0.0", "X-Callayers-Version: 9.0.0"))
    result = subprocess.run(["apt-get", *options, "update"], text=True, capture_output=True)
    assert result.returncode != 0, "APT accepted tampered signed metadata"
print("APT accepts the signed package index and rejects tampered metadata")
