#!/bin/bash
# Shared package hook. Driver installation and matching headers are handled by
# the distro package manager and the installer before this hook runs.
set -euo pipefail
camera_name() { tr -d '\0\n' < /sys/class/video4linux/video42/name 2>/dev/null || true; }
if [ -e /dev/video42 ] && [ "$(camera_name)" != Callayers ]; then
  echo 'The Callayers camera number is unavailable.' >&2
  exit 1
fi
if [ ! -e /dev/video42 ]; then
  if lsmod | awk '{print $1}' | grep -qx v4l2loopback; then
    echo 'Another virtual-camera setup is loaded. Restart, then run sudo /usr/lib/callayers/setup-camera.sh.' >&2
    exit 1
  fi
  modprobe v4l2loopback
fi
if [ ! -e /dev/video42 ] || [ "$(camera_name)" != Callayers ]; then
  echo 'The Callayers camera driver did not load. Check the running kernel headers and Secure Boot, then rerun sudo /usr/lib/callayers/setup-camera.sh.' >&2
  exit 1
fi
