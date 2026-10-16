"""Exercise installer platform gates without changing the machine."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class Platforms(unittest.TestCase):
    def check_platform(self, release, *arguments, arch="x86_64"):
        with tempfile.TemporaryDirectory(prefix="callayers-installer-") as directory:
            root = Path(directory)
            os_release = root / "os-release"
            os_release.write_text(release)
            script = Path(__file__).resolve().parents[1] / "linux/packaging/install.sh"
            # Substitute the fixture path only in this disposable copy. The
            # real installer always reads the system's /etc/os-release.
            installer = root / "install.sh"
            installer.write_text(script.read_text().replace("/etc/os-release", str(os_release)))
            commands = root / "bin"
            commands.mkdir()
            for name, body in {
                "uname": f'echo "{arch}"',
                "mokutil": "echo 'SecureBoot disabled'",
                "apt-get": "echo 'ERROR: package manager was invoked'; exit 99",
                "usermod": "echo 'ERROR: account was changed'; exit 99",
            }.items():
                command = commands / name
                command.write_text("#!/bin/sh\n" + body + "\n")
                command.chmod(0o755)
            result = subprocess.run(["bash", str(installer), "--check", *arguments],
                                    env={"PATH": f"{commands}:/usr/bin:/bin"}, text=True, capture_output=True)
            self.assertNotIn("ERROR:", result.stdout + result.stderr)
            return result

    def test_ubuntu_lts(self):
        for version in ("22.04", "24.04", "26.04"):
            with self.subTest(version=version):
                result = self.check_platform(f'ID=ubuntu\nVERSION_ID="{version}"\n')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"ubuntu {version}", result.stdout)

    def test_experimental_requires_opt_in(self):
        for release in (
            'ID=debian\nVERSION_ID="12"\n',
            'ID=debian\nVERSION_ID="13"\n',
            'ID=linuxmint\nVERSION_ID="21.3"\nUBUNTU_CODENAME=jammy\n',
            'ID=linuxmint\nVERSION_ID="22"\nUBUNTU_CODENAME=noble\n',
            'ID=pop\nVERSION_ID="22.04"\nUBUNTU_CODENAME=jammy\n',
            'ID=pop\nVERSION_ID="24.04"\nUBUNTU_CODENAME=noble\n',
        ):
            with self.subTest(release=release):
                rejected = self.check_platform(release)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("--allow-experimental", rejected.stderr)
                allowed = self.check_platform(release, "--allow-experimental")
                self.assertEqual(allowed.returncode, 0, allowed.stderr)
                self.assertIn("WARNING: Experimental", allowed.stderr)
                self.assertIn("not been verified", allowed.stderr)

    def test_unsupported_bases_stay_blocked(self):
        for release in (
            'ID=ubuntu\nVERSION_ID="20.04"\n',
            'ID=ubuntu\nVERSION_ID="25.10"\n',
            'ID=debian\nVERSION_ID="11"\n',
            'ID=linuxmint\nVERSION_ID="20"\nUBUNTU_CODENAME=focal\n',
            'ID=pop\nVERSION_ID="22.04"\n',
            'ID=fedora\nVERSION_ID="44"\nID_LIKE="debian ubuntu"\n',
        ):
            with self.subTest(release=release):
                self.assertNotEqual(self.check_platform(release, "--allow-experimental").returncode, 0)

    def test_arm_stays_blocked(self):
        result = self.check_platform('ID=ubuntu\nVERSION_ID="22.04"\n', arch="aarch64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Intel/AMD x64", result.stderr)


if __name__ == "__main__":
    unittest.main()
