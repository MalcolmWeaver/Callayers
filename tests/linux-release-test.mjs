import test, {before, after} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, rm, readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {publishLinux} from '../scripts/publish-linux.mjs';
import {createReleaseStore, RELEASE_BASE_URL} from '../scripts/release-storage.mjs';

let directory, files;
const inRelease = 'linux/apt/dists/stable/InRelease';
const packageKey = 'linux/apt/pool/main/c/callayers/callayers_1.0.0_amd64.deb';
before(async () => {
  directory = await mkdtemp(path.join(tmpdir(), 'callayers-publish-test-'));
  const result = spawnSync('python3', ['tests/linux-repository-fixture.py', directory], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr);
  directory = path.join(directory, 'repository');
  const manifest = JSON.parse(await readFile(path.join(directory, 'manifest.json')));
  files = new Map(await Promise.all(manifest.files.map(async item => [item.key, await readFile(path.join(directory, item.key))])));
});
after(async () => { if (directory) await rm(path.dirname(directory), {recursive: true, force: true}); });

function fixture(initial = new Map(), failPublic = false) {
  const objects = new Map(initial), writes = [], verified = [];
  return {objects, writes, verified,
    store: {
      async readBytes(key) { return objects.get(key) ?? null; },
      async writeObject(key, bytes) {
        if (key === inRelease) assert.equal(verified.length, files.size - 1, 'Published before public file checks');
        objects.set(key, bytes); writes.push(key);
      }
    },
    async fetchImpl(url) {
      const key = url.slice(RELEASE_BASE_URL.length + 1);
      verified.push(key);
      return failPublic ? new Response(null, {status: 503}) : new Response(objects.get(key), {status: objects.has(key) ? 200 : 404});
    }
  };
}

test('uploads and verifies dependencies before publishing InRelease; preserves prior packages', async () => {
  const prior = 'linux/apt/pool/main/c/callayers/callayers_0.9.0_amd64.deb';
  const state = fixture(new Map([[prior, Buffer.from('old package')]]));
  await publishLinux({directory, ...state});
  assert.equal(state.writes.at(-1), inRelease);
  assert.equal(state.objects.get(prior).toString(), 'old package');
});
test('public download failure leaves existing InRelease untouched', async () => {
  const previous = Buffer.from('X-Callayers-Version: 0.9.0\n');
  const state = fixture(new Map([[inRelease, previous]]), true);
  await assert.rejects(publishLinux({directory, ...state}), /Public repository/);
  assert.equal(state.objects.get(inRelease), previous);
  assert(!state.writes.includes(inRelease));
});
test('same-version package replacement stops before any writes', async () => {
  const state = fixture(new Map([[packageKey, Buffer.from('different bytes')]]));
  await assert.rejects(publishLinux({directory, ...state}), /immutable/);
  assert.deepEqual(state.writes, []);
});
test('downgrade and unexpected signing-key replacement stop publication', async () => {
  for (const [key, bytes] of [[inRelease, Buffer.from('X-Callayers-Version: 2.0.0\n')],
    ['linux/callayers-archive-keyring.gpg', Buffer.from('another key')]]) {
    const state = fixture(new Map([[key, bytes]]));
    await assert.rejects(publishLinux({directory, ...state}), /newer|key changed/);
    assert.deepEqual(state.writes, []);
  }
});
test('Linux and Windows storage writers cannot write across namespaces', async () => {
  const options = {token: 'test', account: 'a'.repeat(32), fetchImpl: async () => new Response(null, {status: 404}), uploadImpl: async () => {}};
  await assert.rejects(createReleaseStore(options).writeObject(packageKey, Buffer.from('x'), '', '', {createOnly: true}), /Unexpected release object/);
  await assert.rejects(createReleaseStore({...options, namespace: 'linux'}).writeObject('tester/latest.json', Buffer.from('x'), '', '', {createOnly: true}), /Unexpected release object/);
  await assert.rejects(createReleaseStore({...options, namespace: 'linux'}).writeObject('linux/../../tester/latest.json', Buffer.from('x'), '', '', {createOnly: true}), /Unexpected release object/);
});
