import {readFile, writeFile, mkdtemp, rm} from 'node:fs/promises';
import {createHash, createPublicKey, verify} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createReleaseStore, getStorageToken, RELEASE_BASE_URL} from './release-storage.mjs';
import {validVersion, compareVersions} from './release-retention.mjs';

const sparkle = 'http://www.andymatuschak.org/xml-namespaces/sparkle';
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const script = name => fileURLToPath(new URL(name, import.meta.url));
const fail = message => { throw Error(message); };
const base64 = (value, size) => {
  if (typeof value !== 'string' || !/^[A-Za-z0-9+/]+={0,2}$/.test(value)) fail('Invalid update signature or public key.');
  const bytes = Buffer.from(value, 'base64');
  if (bytes.length !== size || bytes.toString('base64') !== value) fail('Invalid update signature or public key.');
  return bytes;
};

function releaseMetadata(bytes, arch) {
  if (!bytes || bytes.length > 8192) fail('Invalid Mac release metadata.');
  const value = JSON.parse(bytes);
  if (value.schema !== 1 || value.platform !== 'macos' || value.arch !== arch || value.notarized !== true ||
      !validVersion(value.version) || !/^[1-9]\d*$/.test(value.build) ||
      !/^\d+\.\d+(?:\.\d+)?$/.test(value.minimumSystemVersion) || !/^[a-f0-9]{64}$/.test(value.sha256))
    fail('Invalid Mac release metadata.');
  const name = `Callayers-${value.version}-${value.build}-macos-${arch}.zip`;
  const key = `macos/releases/${value.version}/${arch}/${name}`;
  if (value.url !== `${RELEASE_BASE_URL}/${key}`) fail('Unexpected Mac download URL.');
  return {value, name, key};
}

// Sparkle 2.9.6 signs the exact bytes before its trailing signature comment.
// Verify with the public key embedded in the application; publishing needs no private key.
export function verifyMacFeed(bytes, publicKey) {
  if (!bytes || bytes.length > 1024 * 1024) fail('Invalid signed Mac appcast.');
  const prefix = Buffer.from('<!-- sparkle-signatures:\n');
  const offset = bytes.lastIndexOf(prefix);
  if (offset < 0) fail('Unsigned Mac appcast.');
  const block = bytes.subarray(offset).toString('utf8');
  const match = /^<!-- sparkle-signatures:\nedSignature: ([A-Za-z0-9+/]+={0,2})\nlength: (\d+)\n-->\n?$/.exec(block);
  if (!match || Number(match[2]) !== offset || !verify(null, bytes.subarray(0, offset), publicKey, base64(match[1], 64)))
    fail('Mac appcast signature rejected.');
}

function inspect(archivePath, feedPath) {
  const result = spawnSync('python3', ['-B', script('./publish-macos-inspect.py'), archivePath, feedPath], {encoding: 'utf8', maxBuffer: 2 * 1024 * 1024});
  if (result.error || result.status !== 0) fail(`Could not inspect Mac archive/appcast identity. ${result.stderr ?? result.error ?? ''}`);
  return JSON.parse(result.stdout);
}

function validateItem(item, metadata, archive, publicKey) {
  const enclosure = item?.enclosure;
  if (!item || item.version !== metadata.version || item.build !== metadata.build || item.minimumSystemVersion !== metadata.minimumSystemVersion ||
      enclosure?.url !== metadata.url || enclosure.length !== String(archive.length) || enclosure.type !== 'application/octet-stream' ||
      (enclosure[`{${sparkle}}os`] && enclosure[`{${sparkle}}os`] !== 'macos') ||
      !verify(null, archive, publicKey, base64(enclosure[`{${sparkle}}edSignature`], 64)))
    fail('Mac archive does not match its signed appcast.');
}

export async function loadMacRelease({directory, arch}) {
  if (!['arm64', 'x86_64'].includes(arch)) fail('Choose --arch arm64 or x86_64.');
  directory = path.resolve(directory);
  const latest = await readFile(path.join(directory, 'feed/latest.json'));
  const {value: metadata, name, key} = releaseMetadata(latest, arch);
  const archivePath = path.join(directory, name), feedPath = path.join(directory, 'feed/appcast.xml');
  const [archive, appcast, checksum, notarization] = await Promise.all([
    readFile(archivePath), readFile(feedPath), readFile(`${archivePath}.sha256`), readFile(path.join(directory, 'notarization.json'))
  ]);
  if (sha256(archive) !== metadata.sha256 || checksum.toString() !== `${metadata.sha256}  ${name}\n`)
    fail('Mac archive checksum mismatch.');
  if (JSON.parse(notarization).status !== 'Accepted') fail('Mac notarization was not accepted.');
  const {info, items} = inspect(archivePath, feedPath);
  const feedKey = `macos/${arch}/appcast.xml`, latestKey = `macos/${arch}/latest.json`;
  if (info.CFBundleIdentifier !== 'com.callayers.renderer-preview' || info.CallayersRelease !== 'YES' ||
      info.CFBundleShortVersionString !== metadata.version || info.CFBundleVersion !== metadata.build ||
      info.LSMinimumSystemVersion !== metadata.minimumSystemVersion || info.SUFeedURL !== `${RELEASE_BASE_URL}/${feedKey}` ||
      info.SURequireSignedFeed !== true || info.SUVerifyUpdateBeforeExtraction !== true)
    fail('Mac app identity does not match release metadata.');
  const publicKey = createPublicKey({format: 'der', type: 'spki', key: Buffer.concat([
    Buffer.from('302a300506032b6570032100', 'hex'), base64(info.SUPublicEDKey, 32)
  ])});
  verifyMacFeed(appcast, publicKey);
  if (items.length !== 1) fail('Expected exactly one Mac release in the prepared appcast.');
  validateItem(items[0], metadata, archive, publicKey);
  return {directory, arch, metadata, name, key, archivePath, archive, checksum, appcast, latest, feedKey, latestKey, publicKey, item: items[0]};
}

// Recheck the final ZIP rather than trusting a notarized=true metadata flag.
export async function verifyMacDistribution(release) {
  if (process.platform !== 'darwin') fail('Mac distribution validation requires macOS.');
  const temporary = await mkdtemp(path.join(tmpdir(), 'callayers-publish-'));
  try {
    const app = path.join(temporary, 'Callayers.app');
    for (const [command, args] of [
      ['/usr/bin/ditto', ['-x', '-k', release.archivePath, temporary]],
      ['python3', [script('./check-macos-package.py'), app, '--arch', release.arch, '--distribution']],
      ['xcrun', ['stapler', 'validate', app]],
      ['/usr/sbin/spctl', ['--assess', '--type', 'execute', app]]
    ]) {
      const result = spawnSync(command, args, {encoding: 'utf8', maxBuffer: 4 * 1024 * 1024});
      if (result.error || result.status !== 0) fail(`Mac distribution validation failed: ${path.basename(command)}. ${result.stderr ?? ''}`);
    }
  } finally { await rm(temporary, {recursive: true, force: true}); }
}

export async function publishMac({release, store, fetchImpl = fetch}) {
  const {metadata, key, archive, checksum, appcast, latest, feedKey, latestKey, publicKey} = release;
  const priorLatest = await store.readBytes(latestKey), priorFeed = await store.readBytes(feedKey);
  if (priorLatest) {
    const prior = releaseMetadata(priorLatest, release.arch).value;
    if (!priorFeed) fail('Published Mac metadata has no signed appcast.');
    if (compareVersions(prior.version, metadata.version) > 0 || BigInt(prior.build) > BigInt(metadata.build)) fail('A newer Mac release is already published.');
    if (prior.build === metadata.build && (prior.version !== metadata.version || prior.sha256 !== metadata.sha256)) fail('Published Mac builds are immutable.');
  }
  if (priorFeed) {
    verifyMacFeed(priorFeed, publicKey); // Refuse accidental update-key rotation.
    // Read the existing signed item's version before any write, including partial-publication retries.
    const temporary = await mkdtemp(path.join(tmpdir(), 'callayers-prior-feed-'));
    try {
      const previousPath = path.join(temporary, 'appcast.xml'); await writeFile(previousPath, priorFeed);
      const {items} = inspect(release.archivePath, previousPath);
      if (items.length !== 1 || !validVersion(items[0].version) || !/^[1-9]\d*$/.test(items[0].build)) fail('Invalid published Mac appcast.');
      if (compareVersions(items[0].version, metadata.version) > 0 || BigInt(items[0].build) > BigInt(metadata.build)) fail('A newer Mac appcast is already published.');
      if (items[0].build === metadata.build && (items[0].version !== metadata.version || items[0].enclosure.url !== metadata.url ||
          items[0].enclosure[`{${sparkle}}edSignature`] !== release.item.enclosure[`{${sparkle}}edSignature`]))
        fail('Published Mac builds are immutable.');
    } finally { await rm(temporary, {recursive: true, force: true}); }
  }
  const immutable = [[key, archive, 'application/zip'], [`${key}.sha256`, checksum, 'text/plain']];
  const previous = new Map();
  for (const [objectKey, bytes] of immutable) {
    const existing = await store.readBytes(objectKey); previous.set(objectKey, existing);
    if (existing && !existing.equals(bytes)) fail('Published Mac archives are immutable.');
  }
  const checkPublic = async (objectKey, bytes) => {
    const response = await fetchImpl(`${RELEASE_BASE_URL}/${objectKey}`, {cache: 'no-store', credentials: 'omit', redirect: 'error', signal: AbortSignal.timeout(120000)});
    if (!response.ok || sha256(Buffer.from(await response.arrayBuffer())) !== sha256(bytes)) fail(`Public Mac file unavailable or mismatched: ${objectKey}.`);
  };
  for (const [objectKey, bytes, type] of immutable) {
    if (!previous.get(objectKey)) await store.writeObject(objectKey, bytes, type, 'public,max-age=31536000,immutable', {createOnly: true});
    await checkPublic(objectKey, bytes);
  }
  // The updater head advances only after exact public archive verification.
  if (!priorFeed?.equals(appcast)) await store.writeObject(feedKey, appcast, 'application/xml', 'no-store', {createOnly: priorFeed === null});
  await checkPublic(feedKey, appcast);
  // Website availability is last; a failure before here preserves its previous download.
  if (!priorLatest?.equals(latest)) await store.writeObject(latestKey, latest, 'application/json', 'no-store', {createOnly: priorLatest === null});
  await checkPublic(latestKey, latest);
  return metadata.url;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const args = process.argv.slice(2), dryRun = args.includes('--dry-run');
  const position = args.indexOf('--arch');
  if (position !== 1 || ![3, 4].includes(args.length) || (args.length === 4 && args[3] !== '--dry-run'))
    fail('Usage: node scripts/publish-macos.mjs <release-directory> --arch arm64|x86_64 [--dry-run]');
  const release = await loadMacRelease({directory: args[0], arch: args[2]});
  await verifyMacDistribution(release);
  if (dryRun) console.log(`Validated Mac ${release.arch} ${release.metadata.version} (${release.metadata.build}); no network publication performed.\n${release.metadata.url}`);
  else {
    const url = await publishMac({release, store: createReleaseStore({token: getStorageToken(), namespace: 'macos'})});
    console.log(`Published Mac ${release.arch} ${release.metadata.version} (${release.metadata.build}): ${url}`);
  }
}
