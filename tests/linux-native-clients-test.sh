#!/bin/bash
# Read a signed local repository with the actual native manager. All client
# state stays in a temporary directory; no app/driver installation is performed.
set -euo pipefail
repository=$(realpath "$1")
version=$2
work=$(mktemp -d)
trap 'gpgconf --homedir "$work/gnupg" --kill gpg-agent 2>/dev/null || true; rm -rf "$work"' EXIT
. /etc/os-release
mkdir -p "$work/cache" "$work/state" "$work/log" "$work/repos"
if [ "$ID" = fedora ]; then
  query() {
    dnf -y --setopt="cachedir=$work/cache-$1" --setopt="persistdir=$work/state-$1" --setopt="logdir=$work/log" \
      --setopt="reposdir=$work/repos" --repofrompath="callayers-test,file://$2" \
      --setopt=callayers-test.gpgcheck=1 --setopt=callayers-test.repo_gpgcheck=1 \
      --setopt=callayers-test.skip_if_unavailable=0 \
      --setopt="callayers-test.gpgkey=file://$repository/linux/callayers-archive-key.asc" \
      --repo=callayers-test repoquery --qf '%{version}' callayers
  }
  query good "$repository/linux/rpm" > "$work/query"
  grep -qx "$version" "$work/query"
  mkdir -p "$work/tampered"
  cp -a "$repository/linux/rpm/repodata" "$work/tampered/repodata"
  printf '\n<!-- tampered -->\n' >> "$work/tampered/repodata/repomd.xml"
  if query bad "$work/tampered" > "$work/rejected" 2>&1; then
    echo 'DNF accepted tampered repository metadata' >&2; exit 1
  fi
elif [ "$ID" = arch ]; then
  mkdir -p "$work/gnupg" "$work/db" "$work/arch"
  chmod 700 "$work/gnupg"
  pacman-key --gpgdir "$work/gnupg" --init
  pacman-key --gpgdir "$work/gnupg" --add "$repository/linux/callayers-archive-keyring.gpg"
  fingerprint=$(gpg --homedir "$work/gnupg" --show-keys --with-colons "$repository/linux/callayers-archive-keyring.gpg" | awk -F: '$1=="fpr" {print $10; exit}')
  pacman-key --gpgdir "$work/gnupg" --lsign-key "$fingerprint"
  cp -L "$repository/linux/arch/x86_64/callayers.db" "$repository/linux/arch/x86_64/callayers.db.sig" "$work/arch/"
  cat > "$work/pacman.conf" <<EOF
[options]
Architecture = x86_64
DBPath = $work/db
CacheDir = $work/cache
GPGDir = $work/gnupg
LogFile = $work/log/pacman.log
SigLevel = Required DatabaseRequired
[callayers]
Server = file://$work/arch
EOF
  pacman --config "$work/pacman.conf" -Sy --noconfirm
  pacman --config "$work/pacman.conf" -Si callayers > "$work/query"
  grep -Eq "^Version[[:space:]]*: $version-1$" "$work/query"
  printf tampered >> "$work/arch/callayers.db"
  if pacman --config "$work/pacman.conf" -Syy --noconfirm > "$work/rejected" 2>&1; then
    echo 'pacman accepted tampered repository metadata' >&2; exit 1
  fi
else
  echo 'This client test runs on Fedora or Arch only.' >&2; exit 1
fi
echo "$ID: native client accepts signed repository version $version and rejects tampering"
