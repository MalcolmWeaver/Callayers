#!/bin/bash
# Hosted-container dependency checks; never installs the Callayers package or
# runs its camera hook. The driver package is only downloaded on Fedora.
set -euo pipefail
. /etc/os-release
package=$(realpath "$1")
if [ "$ID" = fedora ]; then
  dnf -y --setopt=install_weak_deps=False install python3 binutils bsdtar qt6-qtbase qt6-qtbase-gui qt6-qtwayland fontconfig libX11 libxcb dejavu-sans-fonts curl gnupg2
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  curl -fsSL --proto '=https' https://download1.rpmfusion.org/free/fedora/RPM-GPG-KEY-rpmfusion-free-fedora-2020 -o "$work/key.asc"
  fingerprint=$(gpg --show-keys --with-colons "$work/key.asc" | awk -F: '$1=="fpr" {print $10; exit}')
  test "$fingerprint" = E9A491A3DE247814E7E067EAE06F8ECDD651FF2E
  rpm --import "$work/key.asc"
  dnf -y --setopt=localpkg_gpgcheck=True install "https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$VERSION_ID.noarch.rpm"
  dnf -y --setopt=install_weak_deps=False install --downloadonly "$package"
  python3 linux/packaging/check-native.py "$package" --format rpm
elif [ "$ID" = arch ]; then
  # A base container has no kernel package. Give DKMS an empty modules directory
  # so its package hook can register sources without a spurious missing-dir error.
  mkdir -p /usr/lib/modules
  pacman -Syu --noconfirm --needed python binutils libarchive zstd qt6-base qt6-wayland fontconfig libx11 libxcb ttf-dejavu v4l2loopback-dkms
  python3 - "$package" <<'PY'
import subprocess, sys
info = subprocess.check_output(['bsdtar', '-xOf', sys.argv[1], '.PKGINFO'], text=True)
dependencies = [line.split(' = ', 1)[1] for line in info.splitlines() if line.startswith('depend = ')]
subprocess.run(['pacman', '-T', *dependencies], check=True)
PY
  python3 linux/packaging/check-native.py "$package" --format arch
else
  echo 'Run this dependency check in Fedora or Arch.' >&2
  exit 1
fi
