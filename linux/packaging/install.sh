#!/bin/bash
# Install Callayers on Ubuntu and load the Callayers camera.
set -euo pipefail

key_url="https://downloads.callayers.com/linux/callayers-archive-keyring.gpg"
repo_line="deb [signed-by=/usr/share/keyrings/callayers-archive-keyring.gpg] https://downloads.callayers.com/linux/apt stable main"
check_only=0
deb=""

while [ $# -gt 0 ]; do
  case "$1" in
    --check) check_only=1 ;;
    --deb) deb="${2:-}"; shift ;;
    *) echo "Usage: install.sh [--check] [--deb package.deb]" >&2; exit 2 ;;
  esac
  shift
done

if [ -r /etc/os-release ]; then
  # shellcheck disable=SC1091
  . /etc/os-release
else
  echo "Callayers for Linux requires Ubuntu 24.04 or 26.04 on x64." >&2
  exit 1
fi

version_id="${VERSION_ID:-}"
arch="$(uname -m)"
if [ "${ID:-}" != "ubuntu" ] || { [ "$version_id" != "24.04" ] && [ "$version_id" != "26.04" ]; } || [ "$arch" != "x86_64" ]; then
  echo "Callayers for Linux requires Ubuntu 24.04 or 26.04 on x64." >&2
  exit 1
fi

secure_boot=0
secure_known=0
sb_var="$(echo /sys/firmware/efi/efivars/SecureBoot-* 2>/dev/null || true)"
if [ -n "$sb_var" ] && [ -r "$sb_var" ]; then
  secure_known=1
  state="$(od -An -t u1 -j 4 -N 1 "$sb_var" | tr -d ' ')"
  [ "$state" = "1" ] && secure_boot=1
elif command -v mokutil >/dev/null 2>&1; then
  secure_known=1
  if mokutil --sb-state 2>/dev/null | grep -q 'enabled'; then secure_boot=1; fi
fi

if [ "$check_only" = "1" ]; then
  echo "Callayers Linux install check"
  echo "Ubuntu ${version_id} ${arch}"
  if [ "$secure_known" = "1" ]; then
    echo "Secure Boot: $([ "$secure_boot" = 1 ] && echo on || echo off)"
  else
    echo "Secure Boot: unknown until the installer runs as root"
  fi
  echo "Would install linux-headers-$(uname -r)"
  if [ -n "$deb" ]; then echo "Would install local package $deb"; else echo "Would install the Callayers apt repository and package"; fi
  exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
  echo "Run this installer with sudo." >&2
  exit 1
fi

if [ "$secure_boot" = "1" ]; then
  echo "The Callayers camera needs the v4l2loopback module, and Secure Boot is blocking it." >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y "linux-headers-$(uname -r)"
if apt-cache show linux-headers-generic >/dev/null 2>&1; then
  apt-get install -y linux-headers-generic
fi

if [ -n "$deb" ]; then
  apt-get install -y "$deb"
else
  apt-get install -y ca-certificates curl
  install -d -m 0755 /usr/share/keyrings
  curl -fsSL "$key_url" -o /usr/share/keyrings/callayers-archive-keyring.gpg
  echo "$repo_line" > /etc/apt/sources.list.d/callayers.list
  apt-get update
  apt-get install -y callayers
fi

if [ -n "${SUDO_USER:-}" ] && ! id -nG "$SUDO_USER" | tr ' ' '\n' | grep -qx video; then
  usermod -aG video "$SUDO_USER"
  echo "Log out and back in so you can open the webcam."
fi

echo "Callayers is installed. Open it, sign in, and choose Callayers in Zoom."
