#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
version="0.3.0"
build="$root/build/linux"
cmake -S "$root/linux" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCALLAYERS_DEVELOPMENT=OFF
cmake --build "$build" --parallel
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
cmake --install "$build" --prefix "$stage/usr"
mkdir -p "$stage/DEBIAN" "$stage/etc/modprobe.d" "$stage/etc/modules-load.d"
cp "$here/callayers-modprobe.conf" "$stage/etc/modprobe.d/callayers.conf"
cp "$here/modules-load.conf" "$stage/etc/modules-load.d/callayers.conf"
cp "$here/postinst" "$stage/DEBIAN/postinst"
chmod 755 "$stage/DEBIAN/postinst"
# These package names exist on Ubuntu 24.04 and 26.04. libqt6widgets6 pulls the
# matching Qt core library, including the t64 rename on newer Ubuntu.
depends="libqt6widgets6, libqt6gui6, libqt6network6, libqt6dbus6, v4l2loopback-dkms"
cat > "$stage/DEBIAN/control" <<EOF
Package: callayers
Version: ${version}
Section: video
Priority: optional
Architecture: amd64
Depends: ${depends}
Maintainer: Callayers <malcolm@callayers.com>
Description: Webcam cards and annotations for video calls
 Interactive cards and annotations over a webcam, shown in Zoom as the Callayers camera.
EOF
mkdir -p "$root/build/linux"
dpkg-deb --root-owner-group --build "$stage" "$root/build/linux/callayers_${version}_amd64.deb"
echo "$root/build/linux/callayers_${version}_amd64.deb"
