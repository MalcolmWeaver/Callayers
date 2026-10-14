import {readFile} from 'node:fs/promises';
import {createHash, createPublicKey, verify} from 'node:crypto';
import path from 'node:path';
import {createReleaseStore, RETENTION_KEY, RELEASE_BASE_URL, prepareRetention, compareVersions,
  cleanupReleases, validVersion, installerKey, feedVersion} from './release-retention.mjs';
import {getStorageToken} from './release-storage.mjs';

const version = process.argv[2];
if (!validVersion(version)) throw Error('Invalid release version.');
const key = installerKey(version), url = `${RELEASE_BASE_URL}/${key}`;
const directory = path.resolve('build/installers', version);
const envelope = await readFile(path.join(directory, 'latest.json'));
const record = JSON.parse(envelope);
const payload = JSON.parse(Buffer.from(record.payload, 'base64').toString('utf8'));
const installer = await readFile(path.join(directory, `CallayersSetup-${version}.exe`));
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
if (payload.schema !== 1 || payload.channel !== 'tester' || payload.version !== version || payload.url !== url
    || payload.size !== installer.length || payload.sha256 !== digest(installer)) throw Error('Installer does not match release manifest.');
const signingKey = JSON.parse(await readFile(path.join(directory, 'public-key.json'), 'utf8'));
const publicKey = createPublicKey({key: {kty: 'RSA', n: Buffer.from(signingKey.Modulus, 'base64').toString('base64url'),
  e: Buffer.from(signingKey.Exponent, 'base64').toString('base64url')}, format: 'jwk'});
if (!verify('sha256', Buffer.from(record.payload, 'base64'), publicKey, Buffer.from(record.signature, 'base64'))) throw Error('Manifest signature rejected.');

const store = createReleaseStore({token: getStorageToken()});
const previous = await store.readJson('tester/latest.json');
const retention = await store.readJson(RETENTION_KEY);
const priorVersion = previous ? feedVersion(previous) : null;
if (previous) {
  const prior = JSON.parse(Buffer.from(previous.payload, 'base64').toString('utf8'));
  if (compareVersions(priorVersion, version) > 0) throw Error('A newer tester release is already published.');
  if (priorVersion === version && prior.sha256 !== payload.sha256) throw Error('Published versions are immutable. Choose a new version.');
}
const existing = await store.readBytes(key);
if (existing && digest(existing) !== payload.sha256) throw Error('This version already exists with different bytes. Publish a new version.');
if (!existing) await store.writeObject(key, installer, 'application/vnd.microsoft.portable-executable',
  'public,max-age=31536000,immutable', {createOnly: true});
// Verify the exact anonymous HTTPS URL that the desktop will use before publishing metadata.
const publicFile = await fetch(url, {cache: 'no-store', redirect: 'error', signal: AbortSignal.timeout(120000)});
if (!publicFile.ok) throw Error('Public installer is unavailable; update feed was not changed.');
if (digest(Buffer.from(await publicFile.arrayBuffer())) !== payload.sha256) throw Error('Public installer checksum mismatch; update feed was not changed.');
const nextRetention = prepareRetention(retention, version, priorVersion, await store.listInstallers());
await store.writeRetention(nextRetention);
await store.writeObject('tester/latest.json', envelope, 'application/json', 'no-store');
console.log(`Published tester release ${version}: ${url}`);
await cleanupReleases(store);
