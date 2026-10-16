"""Exercise installer command ordering in a disposable fake host filesystem."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class Actions(unittest.TestCase):
    def install(self, release, *arguments, fusion=False, secure=False, key_ok=True, reboot=False):
        with tempfile.TemporaryDirectory(prefix='callayers-install-actions-') as directory:
            root = Path(directory)
            for name in ['etc/yum.repos.d', 'etc/pki/rpm-gpg', 'etc/pacman.d', 'usr/share/keyrings',
                         'usr/lib/modules/6.17.0-test/build', 'bin', 'sys/firmware/efi/efivars', 'run']:
                (root / name).mkdir(parents=True, exist_ok=True)
            (root / 'etc/os-release').write_text(release)
            (root / 'etc/pacman.conf').write_text('[options]\n')
            (root / 'usr/lib/modules/6.17.0-test/pkgbase').write_text('linux-lts\n')
            header = root / 'usr/lib/modules/6.17.0-test/build/Makefile'
            header.touch()
            script = (Path(__file__).resolve().parents[1] / 'linux/packaging/install.sh').read_text()
            for prefix in ['/etc/', '/usr/share/keyrings', '/usr/lib/modules', '/sys/', '/run/']:
                script = script.replace(prefix, str(root) + prefix)
            (root / 'install.sh').write_text(script)
            bodies = {
                'uname': 'if [ "$1" = -m ]; then echo x86_64; else echo 6.17.0-test; fi',
                'id': 'if [ "$1" = -u ]; then echo 0; else echo video; fi',
                'mokutil': 'echo "SecureBoot $SECURE_STATE"',
                'rpm': 'if [ "$1" = -q ]; then [ "$FUSION_INSTALLED" = 1 ]; fi',
                'gpg': 'printf "fpr:::::::::%s:\\n" "$KEY_FINGERPRINT"',
                'curl': 'while [ "$#" -gt 0 ]; do if [ "$1" = -o ]; then shift; printf key > "$1"; fi; shift; done',
                'apt-get': 'true', 'dnf': 'true', 'akmods': 'true', 'dkms': 'true', 'pacman-key': 'true',
                'pacman': 'if [ "$SIMULATE_REBOOT" = 1 ]; then rm -f "$FAKE_HEADER"; fi',
            }
            for name, body in bodies.items():
                file = root / 'bin' / name
                file.write_text('#!/bin/bash\nprintf "%s" "${0##*/}" >> "$COMMAND_LOG"\nprintf " <%s>" "$@" >> "$COMMAND_LOG"\nprintf "\\n" >> "$COMMAND_LOG"\n' + body + '\n')
                file.chmod(0o755)
            env = dict(os.environ, PATH=f'{root}/bin:/usr/bin:/bin', COMMAND_LOG=str(root/'commands'),
                       FUSION_INSTALLED=str(int(fusion)), SECURE_STATE='enabled' if secure else 'disabled',
                       KEY_FINGERPRINT='E9A491A3DE247814E7E067EAE06F8ECDD651FF2E' if key_ok else 'A'*40,
                       SIMULATE_REBOOT=str(int(reboot)), FAKE_HEADER=str(header))
            env.pop('SUDO_USER', None)
            result = subprocess.run(['bash', str(root/'install.sh'), *arguments], env=env, text=True, capture_output=True)
            commands = (root/'commands').read_text()
            configs = {str(p.relative_to(root)): p.read_text() for p in (root/'etc').rglob('*') if p.is_file()}
            return result, commands, configs

    def test_fedora_requires_repository_opt_in_before_any_package_install(self):
        result, commands, _ = self.install('ID=fedora\nVERSION_ID=44\n', '--allow-experimental')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--enable-rpmfusion', result.stderr)
        self.assertNotIn('dnf <', commands)

    def test_fedora_opt_in_checks_key_and_enables_only_free_repository(self):
        result, commands, configs = self.install('ID=fedora\nVERSION_ID=44\n', '--allow-experimental', '--enable-rpmfusion')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('community repository', result.stdout)
        self.assertIn('rpmfusion-free-release-44.noarch.rpm', commands)
        self.assertNotIn('nonfree', commands)
        self.assertIn('<--setopt=localpkg_gpgcheck=True>', commands)
        self.assertLess(commands.index('akmods <'), commands.index('dnf <-y> <install> <callayers>'))
        self.assertIn('gpgcheck=1\nrepo_gpgcheck=1', configs['etc/yum.repos.d/callayers.repo'])
        failed, bad_commands, _ = self.install('ID=fedora\nVERSION_ID=44\n', '--allow-experimental', '--enable-rpmfusion', key_ok=False)
        self.assertNotEqual(failed.returncode, 0)
        self.assertNotIn('rpm <--import>', bad_commands)

    def test_existing_fedora_repository_needs_no_enable_flag(self):
        result, commands, _ = self.install('ID=fedora\nVERSION_ID=43\n', '--allow-experimental', fusion=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('rpmfusion-free-release-43.noarch.rpm', commands)

    def test_arch_updates_fully_and_keeps_signature_enforcement(self):
        result, commands, configs = self.install('ID=arch\n', '--allow-experimental')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('pacman <-Syu>', commands)
        self.assertIn('<linux-lts-headers>', commands)
        self.assertLess(commands.index('pacman <-Syu>'), commands.index('dkms <autoinstall>'))
        self.assertIn('SigLevel = Required DatabaseRequired', configs['etc/pacman.d/callayers.conf'])
        self.assertIn('Include = ', configs['etc/pacman.conf'])
        self.assertNotIn('TrustAll', str(configs))

    def test_arch_requires_reboot_before_app_install_after_header_mismatch(self):
        result, commands, _ = self.install('ID=arch\n', '--allow-experimental', reboot=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Reboot', result.stderr)
        self.assertNotIn('<callayers>', commands)

    def test_secure_boot_stops_before_package_changes(self):
        result, commands, _ = self.install('ID=ubuntu\nVERSION_ID=24.04\n', secure=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Secure Boot', result.stderr)
        self.assertNotIn('apt-get <', commands)

if __name__ == '__main__':
    unittest.main()
