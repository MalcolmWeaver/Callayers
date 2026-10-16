#!/bin/bash
# Shared native installer. Each backend uses its own package manager and driver.
set -euo pipefail
base="https://downloads.callayers.com/linux"
check_only=0
package=""
allow_experimental=0
enable_rpmfusion=0
while [ $# -gt 0 ]; do
  case "$1" in
    --check) check_only=1 ;;
    --allow-experimental) allow_experimental=1 ;;
    --enable-rpmfusion) enable_rpmfusion=1 ;;
    --deb|--package)
      [ $# -ge 2 ] && [ -f "$2" ] || { echo "$1 requires an existing package" >&2; exit 2; }
      package="$(realpath "$2")"; shift ;;
    *) echo 'Usage: install.sh [--check] [--package package.deb|package.rpm|package.pkg.tar.zst] [--allow-experimental] [--enable-rpmfusion]' >&2; exit 2 ;;
  esac
  shift
done
[ -r /etc/os-release ] || { echo 'Cannot identify this Linux distribution.' >&2; exit 1; }
. /etc/os-release
version_id="${VERSION_ID:-rolling}"
arch="$(uname -m)"
kernel="$(uname -r)"
[ "$arch" = x86_64 ] || { echo 'Callayers requires an Intel/AMD x64 computer.' >&2; exit 1; }
experimental=0
backend=apt
case "${ID:-}:$version_id" in
  ubuntu:22.04|ubuntu:24.04|ubuntu:26.04) ;;
  debian:12|debian:13) experimental=1 ;;
  linuxmint:*|pop:*)
    case "${UBUNTU_CODENAME:-}" in
      jammy|noble|resolute) experimental=1 ;;
      *) echo 'This system needs an Ubuntu 22.04, 24.04, or 26.04 base for Callayers.' >&2; exit 1 ;;
    esac ;;
  fedora:43|fedora:44)
    if [ -e /run/ostree-booted ] || [[ "${VARIANT_ID:-}" =~ (silverblue|kinoite|coreos|atomic|sericea|onyx) ]]; then
      echo 'This experimental installer supports traditional Fedora desktops, not Atomic/OSTree editions.' >&2; exit 1
    fi
    backend=dnf; experimental=1 ;;
  arch:*) backend=pacman; experimental=1 ;;
  *) echo 'Supported: Ubuntu 22.04/24.04/26.04 x64. Experimental: Debian 12/13, compatible Mint/Pop!_OS, Fedora 43/44, and Arch Linux.' >&2; exit 1 ;;
esac
if [ "$experimental" = 1 ]; then
  [ "$allow_experimental" = 1 ] || { echo "${PRETTY_NAME:-${ID}} is experimental. Rerun with --allow-experimental to opt in." >&2; exit 1; }
  echo "WARNING: Experimental installation on ${PRETTY_NAME:-${ID}}. Desktop, sign-in, and camera-driver behavior have not been verified on this distribution." >&2
fi
if [ -n "$package" ]; then
  case "$backend:$package" in
    apt:*.deb|dnf:*.rpm|pacman:*.pkg.tar.zst) ;;
    *) echo "The local package does not match the $backend backend." >&2; exit 2 ;;
  esac
fi
if [ "$enable_rpmfusion" = 1 ] && [ "$backend" != dnf ]; then
  echo '--enable-rpmfusion is only valid for Fedora.' >&2; exit 2
fi
secure_boot=0
secure_known=0
sb_var="$(echo /sys/firmware/efi/efivars/SecureBoot-* 2>/dev/null || true)"
if [ -n "$sb_var" ] && [ -r "$sb_var" ]; then
  secure_known=1
  state="$(od -An -t u1 -j 4 -N 1 "$sb_var" | tr -d ' ')"
  [ "$state" = 1 ] && secure_boot=1
elif command -v mokutil >/dev/null 2>&1; then
  secure_known=1
  if mokutil --sb-state 2>/dev/null | grep -q enabled; then secure_boot=1; fi
fi
if [ "$check_only" = 1 ]; then
  echo 'Callayers Linux install check'
  echo "${PRETTY_NAME:-${ID} ${version_id}} ${arch}"
  echo "Package manager: $backend"
  if [ "$secure_known" = 1 ]; then echo "Secure Boot: $([ "$secure_boot" = 1 ] && echo on || echo off)"; else echo 'Secure Boot: unknown until the installer runs as root'; fi
  case "$backend" in
    apt) echo "Would install linux-headers-$kernel and v4l2loopback-dkms" ;;
    dnf)
      echo "Would install kernel-devel-$kernel and akmod-v4l2loopback from RPM Fusion Free"
      if [ "$enable_rpmfusion" = 1 ]; then echo 'Would enable RPM Fusion Free if missing (explicit opt-in).'; else echo 'Requires RPM Fusion Free already installed; --enable-rpmfusion explicitly opts into enabling it.'; fi ;;
    pacman) echo "Would perform a full Arch upgrade and install v4l2loopback-dkms and headers matching $kernel; reboot and rerun if a kernel upgrade removes matching headers." ;;
  esac
  if [ -n "$package" ]; then echo "Would install local package $package"; else echo "Would configure the signed Callayers $backend repository and install the native package"; fi
  exit 0
fi
[ "$(id -u)" = 0 ] || { echo 'Run this installer with sudo.' >&2; exit 1; }
[ "$secure_boot" = 0 ] || { echo 'The Callayers camera needs the v4l2loopback module, and Secure Boot is blocking it.' >&2; exit 1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fetch() { curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' "$1" -o "$2"; }
case "$backend" in
  apt)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y "linux-headers-$kernel"
    if [ -n "$package" ]; then apt-get install -y "$package"; else
      apt-get install -y ca-certificates curl
      fetch "$base/callayers-archive-keyring.gpg" "$work/key.gpg"
      install -d -m 0755 /usr/share/keyrings
      install -m 0644 "$work/key.gpg" /usr/share/keyrings/callayers-archive-keyring.gpg
      echo 'deb [signed-by=/usr/share/keyrings/callayers-archive-keyring.gpg] https://downloads.callayers.com/linux/apt stable main' > /etc/apt/sources.list.d/callayers.list
      apt-get update
      apt-get install -y callayers
    fi ;;
  dnf)
    if ! rpm -q rpmfusion-free-release >/dev/null 2>&1; then
      if [ "$enable_rpmfusion" != 1 ]; then
        echo 'The virtual camera requires RPM Fusion Free. Set it up yourself, or rerun with --allow-experimental --enable-rpmfusion to explicitly enable this community repository.' >&2
        exit 1
      fi
      echo 'Enabling RPM Fusion Free, a community repository, to obtain the virtual-camera driver.'
      dnf -y install ca-certificates curl gnupg2
      fetch 'https://download1.rpmfusion.org/free/fedora/RPM-GPG-KEY-rpmfusion-free-fedora-2020' "$work/rpmfusion.asc"
      fusion_fingerprint="$(gpg --show-keys --with-colons "$work/rpmfusion.asc" | awk -F: '$1=="fpr" {print $10; exit}')"
      [ "$fusion_fingerprint" = E9A491A3DE247814E7E067EAE06F8ECDD651FF2E ] || { echo 'RPM Fusion Free signing key did not match the expected fingerprint.' >&2; exit 1; }
      rpm --import "$work/rpmfusion.asc"
      dnf -y --setopt=localpkg_gpgcheck=True install "https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$version_id.noarch.rpm"
    fi
    if ! dnf -y install "kernel-devel-$kernel" akmod-v4l2loopback; then
      echo 'Matching Fedora kernel development files and the camera driver could not be installed. Check that RPM Fusion Free is enabled. If headers are unavailable, update Fedora, reboot into the current kernel, and rerun this installer.' >&2
      exit 1
    fi
    akmods --force --kernels "$kernel"
    if [ -n "$package" ]; then dnf -y install "$package"; else
      dnf -y install ca-certificates curl
      fetch "$base/callayers-archive-key.asc" "$work/key.asc"
      install -d -m 0755 /etc/pki/rpm-gpg /etc/yum.repos.d
      install -m 0644 "$work/key.asc" /etc/pki/rpm-gpg/RPM-GPG-KEY-callayers
      rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-callayers
      cat > /etc/yum.repos.d/callayers.repo <<'REPO'
[callayers]
name=Callayers experimental
baseurl=https://downloads.callayers.com/linux/rpm
enabled=1
gpgcheck=1
repo_gpgcheck=1
skip_if_unavailable=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-callayers
REPO
      dnf -y install callayers
    fi ;;
  pacman)
    header=''
    if [ -r "/usr/lib/modules/$kernel/pkgbase" ]; then
      kernel_package="$(cat "/usr/lib/modules/$kernel/pkgbase")"
      case "$kernel_package" in linux|linux-lts|linux-zen|linux-hardened) header="$kernel_package-headers" ;; esac
    fi
    if [ -z "$header" ] && [ ! -r "/usr/lib/modules/$kernel/build/Makefile" ]; then
      echo 'Install the matching headers for your running Arch kernel, then rerun the installer.' >&2; exit 1
    fi
    echo 'Arch requires a full system upgrade when installing new packages. A kernel upgrade may require rebooting and rerunning this installer.'
    pacman -Syu --noconfirm --needed ca-certificates curl gnupg v4l2loopback-dkms ${header:+"$header"}
    if [ ! -r "/usr/lib/modules/$kernel/build/Makefile" ]; then
      echo 'The running kernel no longer has matching headers. Reboot into the upgraded kernel, then rerun the installer.' >&2; exit 1
    fi
    dkms autoinstall -k "$kernel"
    if [ -n "$package" ]; then pacman -U --noconfirm "$package"; else
      fetch "$base/callayers-archive-keyring.gpg" "$work/key.gpg"
      fingerprint="$(gpg --show-keys --with-colons "$work/key.gpg" | awk -F: '$1=="fpr" {print $10; exit}')"
      [[ "$fingerprint" =~ ^[A-F0-9]{40}$ ]] || { echo 'Invalid Callayers signing key.' >&2; exit 1; }
      pacman-key --add "$work/key.gpg"
      pacman-key --lsign-key "$fingerprint"
      install -d -m 0755 /etc/pacman.d
      cat > /etc/pacman.d/callayers.conf <<'REPO'
[callayers]
SigLevel = Required DatabaseRequired
Server = https://downloads.callayers.com/linux/arch/x86_64
REPO
      if ! grep -qxF 'Include = /etc/pacman.d/callayers.conf' /etc/pacman.conf; then
        printf '\nInclude = /etc/pacman.d/callayers.conf\n' >> /etc/pacman.conf
      fi
      pacman -Syu --noconfirm --needed callayers
    fi ;;
esac
if [ -n "${SUDO_USER:-}" ] && ! id -nG "$SUDO_USER" | tr ' ' '\n' | grep -qx video; then
  usermod -aG video "$SUDO_USER"
  echo 'Log out and back in so you can open the webcam.'
fi
echo 'Callayers is installed. Open it, sign in, and choose Callayers in your calling app.'
if [ -n "$package" ]; then echo 'Local test package installed; rerun the public installer after publication to enable repository updates.'; fi
