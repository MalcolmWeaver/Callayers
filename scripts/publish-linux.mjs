import {readFile, mkdtemp, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createReleaseStore, getStorageToken, isLinuxReleaseKey, RELEASE_BASE_URL} from './release-storage.mjs';

const versionPattern = /^(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/;
const root = 'linux/apt/dists/stable/';
const keyring = 'linux/callayers-archive-keyring.gpg';
const sha256 = data => createHash('sha256').update(data).digest('hex');
const immutableKey = key => key === keyring || key === 'linux/callayers-archive-key.asc' || key.includes('/pool/') || key.includes('/by-hash/') ||
  key.startsWith('linux/rpm/packages/') || /\/repodata\/[a-f0-9]{64}-/.test(key) || /\.pkg\.tar\.zst(\.sig)?$/.test(key);
const releaseVersion = bytes => bytes?.toString().match(/^X-Callayers-Version: (\d+\.\d+\.\d+)$/m)?.[1];
const compare = (a, b) => {
  const left = a.split('.').map(Number), right = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if (left[i] !== right[i]) return left[i] - right[i];
  return 0;
};

export async function publishLinux({directory, store, fetchImpl = fetch}) {
  directory = path.resolve(directory);
  const manifest = JSON.parse(await readFile(path.join(directory, 'manifest.json')));
  if (!versionPattern.test(manifest.version) || !Array.isArray(manifest.files)) throw Error('Invalid Linux release manifest.');
  const files = new Map();
  for (const item of manifest.files) {
    if (!isLinuxReleaseKey(item.key) || files.has(item.key)) throw Error('Unexpected or duplicate repository file.');
    const bytes = await readFile(path.join(directory, item.key));
    if (sha256(bytes) !== item.sha256 || bytes.length !== item.size) throw Error('Repository checksum mismatch.');
    files.set(item.key, bytes);
  }
  const packageKey = `linux/apt/pool/main/c/callayers/callayers_${manifest.version}_amd64.deb`;
  for (const key of [packageKey, keyring, 'linux/install.sh', `${root}InRelease`, `${root}Release`, `${root}Release.gpg`, `${root}main/binary-amd64/Packages`])
    if (!files.has(key)) throw Error(`Missing repository file: ${key}`);
  if (releaseVersion(files.get(`${root}Release`)) !== manifest.version) throw Error('Release version mismatch.');
  for (const name of ['Packages', 'Packages.gz']) {
    const indexKey = `${root}main/binary-amd64/${name}`;
    const bytes = files.get(indexKey);
    if (!bytes || !files.get(`${root}main/binary-amd64/by-hash/SHA256/${sha256(bytes)}`)?.equals(bytes))
      throw Error('Missing or mismatched by-hash index.');
    const line = ` ${sha256(bytes)} ${bytes.length} main/binary-amd64/${name}\n`;
    if (!files.get(`${root}Release`).toString().includes(line)) throw Error('Index does not match signed Release.');
  }
  const index = files.get(`${root}main/binary-amd64/Packages`).toString();
  for (const line of [`Package: callayers`, `Version: ${manifest.version}`, `Architecture: amd64`,
    `Filename: ${packageKey.slice('linux/apt/'.length)}`, `Size: ${files.get(packageKey).length}`, `SHA256: ${sha256(files.get(packageKey))}`])
    if (!index.split('\n').includes(line)) throw Error('Package does not match repository index.');
  // Verify signature against the exact Release bytes before any remote writes.
  const verifyHome = await mkdtemp(path.join(tmpdir(), 'callayers-verify-'));
  try {
    const verified = spawnSync('gpgv', ['--homedir', verifyHome, '--keyring', path.join(directory, keyring),
      '--output', '-', path.join(directory, `${root}InRelease`)], {maxBuffer: 1024 * 1024});
    if (verified.status !== 0 || !verified.stdout.equals(files.get(`${root}Release`))) throw Error('APT signature rejected.');
    const native = files.get(`${root}Release`).toString().includes('X-Callayers-Native: fedora arch\n');
    const hasNative = [...files.keys()].some(key => key.startsWith('linux/rpm/') || key.startsWith('linux/arch/'));
    if (native !== hasNative) throw Error('Native repositories and signed capability metadata must agree.');
    if (native) {
      const rpm = `linux/rpm/packages/callayers-${manifest.version}-1.x86_64.rpm`;
      const arch = `linux/arch/x86_64/callayers-${manifest.version}-1-x86_64.pkg.tar.zst`;
      for (const required of [rpm, arch, `${arch}.sig`, 'linux/callayers-archive-key.asc',
        'linux/rpm/repodata/repomd.xml', 'linux/rpm/repodata/repomd.xml.asc',
        'linux/arch/x86_64/callayers.db', 'linux/arch/x86_64/callayers.db.sig'])
        if (!files.has(required)) throw Error(`Missing native repository file: ${required}`);
      const indices = spawnSync('python3', [new URL('../linux/packaging/verify-native-repositories.py', import.meta.url).pathname, directory, manifest.version]);
      if (indices.error || indices.status !== 0) throw Error('Native repository index/signature verification failed.');
    }
  } finally { await rm(verifyHome, {recursive: true, force: true}); }
  const oldRelease = await store.readBytes(`${root}InRelease`);
  const oldVersion = releaseVersion(oldRelease);
  if (oldRelease && (!oldVersion || !versionPattern.test(oldVersion))) throw Error('Existing Linux release is invalid.');
  if (oldVersion && compare(oldVersion, manifest.version) > 0) throw Error('A newer Linux version is already published.');
  const oldKey = await store.readBytes(keyring);
  if (oldKey && !oldKey.equals(files.get(keyring))) throw Error('APT signing key changed; key rotation requires a separate migration.');
  const previous = new Map([[`${root}InRelease`, oldRelease], [keyring, oldKey]]);
  // Preflight every immutable object before uploading anything.
  for (const [key, bytes] of files) {
    if (!previous.has(key)) previous.set(key, await store.readBytes(key));
    const immutable = immutableKey(key);
    if (immutable && previous.get(key) && !previous.get(key).equals(bytes)) throw Error('Published versions and indices are immutable.');
  }
  // By-hash indices and packages are retained. Existing clients can finish an
  // update against the previous InRelease while the next version is uploaded.
  // Upload package/content-hash objects first, then native signatures/heads.
  // Each native manager has its own signed publication head. InRelease goes
  // last and enables the website's Fedora/Arch options only after all succeed.
  const nativeHead = key => /^linux\/rpm\/repodata\/repomd\.xml(\.asc)?$/.test(key) || /^linux\/arch\/x86_64\/callayers\.(db|files)(\.tar\.gz)?(\.sig)?$/.test(key);
  const candidates = [...files.keys()].filter(key => key !== `${root}InRelease`);
  const keys = [...candidates.filter(key => !nativeHead(key)), ...candidates.filter(nativeHead).sort((a,b) => Number(!a.endsWith('.sig') && !a.endsWith('.asc')) - Number(!b.endsWith('.sig') && !b.endsWith('.asc')))];
  for (const key of keys) {
    const bytes = files.get(key);
    const immutable = immutableKey(key);
    if (!previous.get(key)?.equals(bytes)) {
      const contentType = key.endsWith('.deb') ? 'application/vnd.debian.binary-package' : 'application/octet-stream';
      await store.writeObject(key, bytes, contentType, immutable ? 'public,max-age=31536000,immutable' : 'no-store',
        {createOnly: previous.get(key) === null});
    }
    const response = await fetchImpl(`${RELEASE_BASE_URL}/${key}`, {cache: 'no-store', redirect: 'error', signal: AbortSignal.timeout(120000)});
    if (!response.ok || sha256(Buffer.from(await response.arrayBuffer())) !== sha256(bytes)) throw Error('Public repository file unavailable or mismatched; InRelease was not changed.');
  }
  // InRelease is the single publication point for supported Ubuntu APT clients.
  await store.writeObject(`${root}InRelease`, files.get(`${root}InRelease`), 'text/plain', 'no-store', {createOnly: oldRelease === null});
  const response = await fetchImpl(`${RELEASE_BASE_URL}/${root}InRelease`, {cache: 'no-store', redirect: 'error', signal: AbortSignal.timeout(30000)});
  if (!response.ok || sha256(Buffer.from(await response.arrayBuffer())) !== sha256(files.get(`${root}InRelease`)))
    throw Error('Release uploaded but public InRelease verification failed; inspect publishing before announcing.');
  console.log(`Published Callayers Linux ${manifest.version}${files.has('linux/rpm/repodata/repomd.xml') ? ' with APT, RPM, and Arch repositories' : ' for Ubuntu 22.04, 24.04, and 26.04 x64'}.`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (process.argv.length !== 3) throw Error('Usage: node scripts/publish-linux.mjs <signed-repository-directory>');
  await publishLinux({directory: process.argv[2], store: createReleaseStore({token: getStorageToken(), namespace: 'linux'})});
}
