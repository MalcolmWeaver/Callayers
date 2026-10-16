#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="$root/build/linux"
version="${1:-}"
if [ $# -gt 1 ] || { [ -n "$version" ] && ! [[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; }; then
  echo "Usage: build-deb.sh [major.minor.patch]" >&2; exit 2
fi
# The public archive uses one package across Ubuntu LTS versions. Building on
# a newer host would silently raise its Qt/glibc requirements.
. /etc/os-release
if [ "${ID:-}" != ubuntu ] || [ "${VERSION_ID:-}" != 22.04 ] || [ "$(uname -m)" != x86_64 ]; then
  echo "Build release packages inside Ubuntu 22.04 x64 to preserve the Ubuntu LTS compatibility baseline. Use the Linux build workflow or a Ubuntu 22.04 build environment." >&2
  exit 1
fi
version_args=()
if [ -n "$version" ]; then version_args=("-DCALLAYERS_RELEASE_VERSION=$version"); fi
cmake_bin="${CALLAYERS_CMAKE:-cmake}"
if ! command -v "$cmake_bin" >/dev/null && [ -x "$root/.deps/toolroot/usr/bin/cmake" ]; then
  cmake_bin="$root/.deps/toolroot/usr/bin/cmake"
  export LD_LIBRARY_PATH="$root/.deps/toolroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
python3 "$here/acquire-renderer.py"
"$cmake_bin" -S "$root/linux" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCALLAYERS_DEVELOPMENT=OFF -DBUILD_TESTING=ON "${version_args[@]}"
"$cmake_bin" --build "$build" --parallel "${CALLAYERS_BUILD_JOBS:-2}"
"$cmake_bin" --build "$build" --target test -- ARGS="--output-on-failure"
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
printf '%s\n' /etc/modprobe.d/callayers.conf /etc/modules-load.d/callayers.conf > "$stage/DEBIAN/conffiles"
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
depends="${depends#shlibs:Depends=}, v4l2loopback-dkms, qt6-qpa-plugins, qt6-wayland, fonts-dejavu-core"
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
python3 "$here/check-package.py" "$build/callayers_${version}_amd64.deb"
echo "$build/callayers_${version}_amd64.deb"
