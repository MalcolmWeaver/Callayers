#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="$root/build/linux"
cmake_bin="${CALLAYERS_CMAKE:-cmake}"
if ! command -v "$cmake_bin" >/dev/null && [ -x "$root/.deps/toolroot/usr/bin/cmake" ]; then
  cmake_bin="$root/.deps/toolroot/usr/bin/cmake"
  export LD_LIBRARY_PATH="$root/.deps/toolroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
python3 "$here/acquire-renderer.py"
"$cmake_bin" -S "$root/linux" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCALLAYERS_DEVELOPMENT=OFF
"$cmake_bin" --build "$build" --parallel "${CALLAYERS_BUILD_JOBS:-2}"
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")"
test -n "$version"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
"$cmake_bin" --install "$build" --prefix "$stage/usr"
mkdir -p "$stage/DEBIAN" "$stage/etc/modprobe.d" "$stage/etc/modules-load.d" "$stage/debian"
cp "$here/callayers-modprobe.conf" "$stage/etc/modprobe.d/callayers.conf"
cp "$here/modules-load.conf" "$stage/etc/modules-load.d/callayers.conf"
cp "$here/postinst" "$stage/DEBIAN/postinst"
chmod 755 "$stage/DEBIAN/postinst"
# Derive minimum system-library versions from this build. A newer Ubuntu build
# must not claim to install on an older release with incompatible Qt/glibc.
cat > "$stage/debian/control" <<EOF_CONTROL
Source: callayers
Section: video
Priority: optional
Maintainer: Callayers <malcolm@callayers.com>

Package: callayers
Architecture: amd64
Description: Webcam cards and annotations
EOF_CONTROL
depends="$(cd "$stage" && dpkg-shlibdeps --ignore-missing-info -O -eusr/bin/callayers -eusr/lib/callayers/libAppCore.so -eusr/lib/callayers/libUltralight.so -eusr/lib/callayers/libWebCore.so -eusr/lib/callayers/libUltralightCore.so -lusr/lib/callayers 2>shlibdeps.log)"
depends="${depends#shlibs:Depends=}, v4l2loopback-dkms"
cat > "$stage/DEBIAN/control" <<EOF_CONTROL
Package: callayers
Version: ${version}
Section: video
Priority: optional
Architecture: amd64
Depends: ${depends}
Maintainer: Callayers <malcolm@callayers.com>
Description: Webcam cards and annotations for video calls
 Interactive cards and annotations over a webcam, shown in Zoom as the Callayers camera.
EOF_CONTROL
rm -r "$stage/debian" "$stage/shlibdeps.log"
dpkg-deb --root-owner-group --build "$stage" "$build/callayers_${version}_amd64.deb"
echo "$build/callayers_${version}_amd64.deb"
