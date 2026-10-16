async function showLinuxRelease() {
  const download = document.getElementById('linux-download');
  const status = document.getElementById('linux-release-status');
  if (!download || !status) return;
  const unavailable = () => { status.textContent = 'Linux: Release status unavailable. Try again shortly.'; };
  try {
    const response = await fetch('https://downloads.callayers.com/linux/apt/dists/stable/InRelease',
      {cache: 'no-store', signal: AbortSignal.timeout(8000)});
    if (!response.ok) {
      if (response.status === 404) status.textContent = 'Linux: Not published yet.';
      else unavailable();
      return;
    }
    const release = await response.text();
    const version = release.match(/^X-Callayers-Version: ((?:0|[1-9]\d{0,4})\.(?:0|[1-9]\d{0,4})\.(?:0|[1-9]\d{0,4}))$/m)?.[1];
    const ubuntu = release.includes('X-Callayers-Ubuntu: 22.04 24.04 26.04\n') &&
      release.includes('Description: Callayers for Ubuntu LTS x64\n') ? '22.04 / 24.04 / 26.04' :
      release.includes('Description: Callayers for Ubuntu 26.04 x64\n') ? '26.04' : null;
    if (!release.startsWith('-----BEGIN PGP SIGNED MESSAGE-----') || !version || !ubuntu) { unavailable(); return; }
    // Availability only: APT verifies the signed metadata and package hashes
    // when installing. The website never supplies a package URL from this text.
    download.hidden = false;
    download.dataset.version = version;
    download.dataset.ubuntu = ubuntu;
    download.dataset.experimental = String(ubuntu !== '26.04');
    download.dataset.native = String(release.includes('X-Callayers-Native: fedora arch\n') && release.includes('X-Callayers-Fedora: 43 44\n'));
    status.textContent = `Linux: ${version} · Beta · x64`;
  } catch { unavailable(); }
  finally { if(typeof window!=='undefined')window.callayersDownloads?.refresh(); }
}
showLinuxRelease();
