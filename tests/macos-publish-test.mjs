import test, {before, after} from 'node:test';
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdtemp, mkdir, rm} from 'node:fs/promises';
import {generateKeyPairSync, createHash, sign} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {loadMacRelease, publishMac, verifyMacFeed} from '../scripts/publish-macos.mjs';
import {createReleaseStore, isMacReleaseKey, RELEASE_BASE_URL} from '../scripts/release-storage.mjs';

const sparkle = 'http://www.andymatuschak.org/xml-namespaces/sparkle';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
let temporary, release;
const keys = generateKeyPairSync('ed25519');
const rawPublic = pair => pair.publicKey.export({format: 'der', type: 'spki'}).subarray(-32).toString('base64');
const signFeed = (content, pair) => Buffer.concat([content, Buffer.from(`<!-- sparkle-signatures:\nedSignature: ${sign(null, content, pair.privateKey).toString('base64')}\nlength: ${content.length}\n-->\n`)]);

async function fixture({arch = 'x86_64', version = '0.4.0', build = '10', pair = keys, infoChange = {}, itemChange = {}} = {}) {
  const directory = await mkdtemp(path.join(temporary, 'release-')); await mkdir(path.join(directory, 'feed'));
  const name = `Callayers-${version}-${build}-macos-${arch}.zip`, archivePath = path.join(directory, name);
  const info = {CFBundleIdentifier: 'com.callayers.renderer-preview', CFBundleShortVersionString: version, CFBundleVersion: build,
    LSMinimumSystemVersion: '12.3', CallayersRelease: 'YES', SURequireSignedFeed: true, SUVerifyUpdateBeforeExtraction: true,
    SUFeedURL: `${RELEASE_BASE_URL}/macos/${arch}/appcast.xml`, SUPublicEDKey: rawPublic(pair), ...infoChange};
  const generated = spawnSync('python3', ['-c', 'import json,sys,plistlib,zipfile; info=json.load(sys.stdin); payload=zipfile.ZipFile(sys.argv[1],"w"); payload.writestr("Callayers.app/Contents/Info.plist",plistlib.dumps(info)); payload.close()', archivePath], {input: JSON.stringify(info), encoding: 'utf8'});
  assert.equal(generated.status, 0, generated.stderr);
  const archive = await readFile(archivePath);
  const metadata = {schema: 1, platform: 'macos', arch, notarized: true, version, build, minimumSystemVersion: '12.3',
    url: `${RELEASE_BASE_URL}/macos/releases/${version}/${arch}/${name}`, sha256: digest(archive)};
  const item = {version, build, minimumSystemVersion: '12.3', url: metadata.url, size: archive.length,
    signature: sign(null, archive, pair.privateKey).toString('base64'), ...itemChange};
  const content = Buffer.from(`<?xml version="1.0"?><rss version="2.0" xmlns:sparkle="${sparkle}"><channel><title>Callayers</title><item><sparkle:version>${item.build}</sparkle:version><sparkle:shortVersionString>${item.version}</sparkle:shortVersionString><sparkle:minimumSystemVersion>${item.minimumSystemVersion}</sparkle:minimumSystemVersion><enclosure url="${item.url}" length="${item.size}" type="application/octet-stream" sparkle:edSignature="${item.signature}"/></item></channel></rss>\n`);
  await Promise.all([
    writeFile(path.join(directory, 'feed/latest.json'), JSON.stringify(metadata, null, 2) + '\n'),
    writeFile(path.join(directory, 'feed/appcast.xml'), signFeed(content, pair)),
    writeFile(`${archivePath}.sha256`, `${metadata.sha256}  ${name}\n`),
    writeFile(path.join(directory, 'notarization.json'), '{"status":"Accepted"}\n')
  ]);
  return {directory, arch, archivePath, item, pair};
}

before(async () => {
  temporary = await mkdtemp(path.join(tmpdir(), 'callayers-mac-publish-test-'));
  release = await loadMacRelease(await fixture());
});
after(async () => { if (temporary) await rm(temporary, {recursive: true, force: true}); });

function remote(initial = new Map(), failKey, mismatch = false) {
  const objects = new Map(initial), writes = [], checks = [], policies = [];
  return {objects, writes, checks, policies,
    store: {
      async readBytes(key) { return objects.get(key) ?? null; },
      async writeObject(key, bytes, type, cache) {
        if (key === release.latestKey) assert(checks.includes(release.feedKey), 'Availability advanced before public appcast verification');
        if (key === release.feedKey) assert(checks.includes(release.key), 'Updater advanced before public ZIP verification');
        objects.set(key, Buffer.from(bytes)); writes.push(key); policies.push({key, type, cache});
      }
    },
    async fetchImpl(url, options) {
      assert.equal(options.redirect, 'error'); assert.equal(options.credentials, 'omit');
      const key = url.slice(RELEASE_BASE_URL.length + 1); checks.push(key);
      if (key === failKey) return mismatch ? new Response('changed bytes') : new Response(null, {status: 503});
      return objects.has(key) ? new Response(objects.get(key)) : new Response(null, {status: 404});
    }
  };
}

test('publishes one architecture with immutable ZIP first, public verification, signed feed, and availability last', async () => {
  const state = remote(new Map([['macos/arm64/latest.json', Buffer.from('other architecture')]]));
  assert.equal(await publishMac({release, ...state}), release.metadata.url);
  assert.deepEqual(state.writes, [release.key, `${release.key}.sha256`, release.feedKey, release.latestKey]);
  assert.deepEqual(state.checks, state.writes);
  assert.equal(state.objects.get('macos/arm64/latest.json').toString(), 'other architecture');
  assert.equal(state.policies[0].cache, 'public,max-age=31536000,immutable');
  assert(state.policies.slice(-2).every(policy => policy.cache === 'no-store'));
});

test('failed or mismatched anonymous ZIP checks preserve both publication heads', async () => {
  for (const mismatch of [false, true]) {
    const state = remote(new Map(), release.key, mismatch);
    await assert.rejects(publishMac({release, ...state}), /Public Mac file/);
    assert(!state.writes.includes(release.feedKey)); assert(!state.writes.includes(release.latestKey));
  }
});

test('appcast public failure leaves website availability unchanged and can be retried', async () => {
  const state = remote(new Map(), release.feedKey);
  await assert.rejects(publishMac({release, ...state}), /Public Mac file/);
  assert(state.objects.has(release.feedKey)); assert(!state.objects.has(release.latestKey));
  const retry = remote(state.objects);
  await publishMac({release, ...retry});
  assert.deepEqual(retry.writes, [release.latestKey]);
  const again = remote(retry.objects); await publishMac({release, ...again}); assert.deepEqual(again.writes, []);
});

test('immutable archive replacement and version/build downgrades stop before writes', async () => {
  const state = remote(new Map([[release.key, Buffer.from('other bytes')]]));
  await assert.rejects(publishMac({release, ...state}), /immutable/); assert.deepEqual(state.writes, []);
  for (const options of [{version: '0.5.0', build: '11'}, {version: '0.4.0', build: '11'}, {version: '0.3.9', build: '11'}]) {
    const newer = await loadMacRelease(await fixture(options));
    const prior = remote(new Map([[newer.latestKey, newer.latest], [newer.feedKey, newer.appcast]]));
    await assert.rejects(publishMac({release, ...prior}), /newer/); assert.deepEqual(prior.writes, []);
    const partial = remote(new Map([[newer.feedKey, newer.appcast]]));
    await assert.rejects(publishMac({release, ...partial}), /newer/); assert.deepEqual(partial.writes, []);
  }
});

test('update key changes and tampered existing feeds stop before writes', async () => {
  const other = await loadMacRelease(await fixture({pair: generateKeyPairSync('ed25519')}));
  for (const appcast of [other.appcast, Buffer.from(release.appcast.toString().replace('Callayers</title>', 'Changed</title>'))]) {
    const state = remote(new Map([[release.feedKey, appcast]]));
    await assert.rejects(publishMac({release, ...state}), /signature/); assert.deepEqual(state.writes, []);
  }
});

test('local validation rejects wrong architecture, development identity, unsigned/tampered feed, missing notarization, and mismatched enclosure', async () => {
  await assert.rejects(loadMacRelease({directory: release.directory, arch: 'arm64'}), /metadata/);
  for (const options of [{infoChange: {CallayersRelease: 'NO'}}, {infoChange: {SURequireSignedFeed: false}},
    {itemChange: {build: '99'}}, {itemChange: {url: 'https://example.invalid/update.zip'}}, {itemChange: {signature: Buffer.alloc(64).toString('base64')}}]) {
    await assert.rejects(loadMacRelease(await fixture(options)), /identity|signed appcast/);
  }
  for (const kind of ['unsigned', 'tampered', 'notarization', 'archive']) {
    const local = await fixture(); const feedPath = path.join(local.directory, 'feed/appcast.xml');
    if (kind === 'unsigned') await writeFile(feedPath, '<?xml version="1.0"?><rss><channel/></rss>');
    if (kind === 'tampered') await writeFile(feedPath, (await readFile(feedPath)).toString().replace('Callayers</title>', 'Changed</title>'));
    if (kind === 'notarization') await writeFile(path.join(local.directory, 'notarization.json'), '{"status":"Invalid"}');
    if (kind === 'archive') await writeFile(local.archivePath, 'changed archive');
    await assert.rejects(loadMacRelease(local));
  }
});

test('both architectures validate independently and reject same-build different bytes', async () => {
  const arm = await loadMacRelease(await fixture({arch: 'arm64'})); assert.equal(arm.arch, 'arm64');
  const other = await loadMacRelease(await fixture({infoChange: {CFBundleName: 'Changed bytes'}}));
  const state = remote(new Map([[release.latestKey, release.latest], [release.feedKey, release.appcast]]));
  await assert.rejects(publishMac({release: other, ...state}), /immutable/); assert.deepEqual(state.writes, []);
});

test('Mac storage keys are constrained and every platform writer stays in its own namespace', async () => {
  const options = {token: 'fixture', account: 'a'.repeat(32), fetchImpl: async () => new Response(null, {status: 404}), uploadImpl: async () => {}};
  for (const key of [release.key, `${release.key}.sha256`, release.feedKey, release.latestKey]) assert(isMacReleaseKey(key));
  for (const key of ['macos/../tester/latest.json', 'macos/arm64/other.json', release.key.replace('/x86_64/', '/arm64/'),
    release.key.replace('Callayers-0.4.0', 'Callayers-0.4.1'), release.key.replace('.zip', '-development.zip')]) assert(!isMacReleaseKey(key));
  for (const [namespace, key] of [['windows', release.key], ['linux', release.key], ['macos', 'tester/latest.json'],
    ['macos', 'linux/install.sh'], ['macos', 'macos/../../tester/latest.json']]) {
    await assert.rejects(createReleaseStore({...options, namespace}).writeObject(key, Buffer.from('x'), '', '', {createOnly: true}), /Unexpected release object/);
  }
  await createReleaseStore({...options, namespace: 'macos'}).writeObject(release.key, Buffer.from('x'), 'application/zip', 'immutable', {createOnly: true});
});

const signer = process.env.CALLAYERS_SPARKLE_SIGNER || fileURLToPath(new URL('../build/macos-camera-x86_64/SourcePackages/artifacts/sparkle/Sparkle/bin/sign_update', import.meta.url));
test('publisher signatures interoperate with the actual pinned Sparkle signer', {skip: process.platform !== 'darwin'}, async () => {
  const local = await fixture(); const key = path.join(local.directory, 'fixture.key');
  await writeFile(key, keys.privateKey.export({format: 'der', type: 'pkcs8'}).subarray(-32).toString('base64'), {mode: 0o600});
  for (const args of [['--verify', '--ed-key-file', key, local.archivePath, local.item.signature],
    ['--verify', '--ed-key-file', key, path.join(local.directory, 'feed/appcast.xml')],
    ['--ed-key-file', key, path.join(local.directory, 'feed/appcast.xml')]]) {
    const result = spawnSync(signer, args, {encoding: 'utf8'}); assert.equal(result.status, 0, result.stderr + result.stdout);
  }
  const signed = await loadMacRelease(local); verifyMacFeed(signed.appcast, signed.publicKey);
});
