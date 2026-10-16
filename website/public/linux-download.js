async function showLinuxRelease() {
  const download = document.getElementById('linux-download');
  const status = document.getElementById('linux-release-status');
  if (!download || !status) return;
  try {
    const response = await fetch('https://downloads.callayers.com/linux/apt/dists/stable/InRelease',
      {cache: 'no-store', signal: AbortSignal.timeout(8000)});
    if (!response.ok) return;
    const release = await response.text();
    const version = release.match(/^X-Callayers-Version: ((?:0|[1-9]\d{0,4})\.(?:0|[1-9]\d{0,4})\.(?:0|[1-9]\d{0,4}))$/m)?.[1];
    if (!release.startsWith('-----BEGIN PGP SIGNED MESSAGE-----') || !version ||
        !release.includes('Description: Callayers for Ubuntu 26.04 x64')) return;
    // Availability only: APT verifies the signed metadata and package hashes
    // when installing. The website never supplies a package URL from this text.
    download.hidden = false;
    status.textContent = `Linux beta ${version} · Ubuntu 26.04 x64`;
  } catch { /* Leave the installation command hidden until publication is reachable. */ }
}
showLinuxRelease();
