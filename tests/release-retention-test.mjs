import test from 'node:test';
import assert from 'node:assert/strict';
import {GRACE_MS, RETENTION_KEY, installerKey, prepareRetention, cleanupReleases,
  createReleaseStore, validateRetention, compareVersions} from '../scripts/release-retention.mjs';

const start = Date.parse('2026-10-14T12:00:00Z');
const objects = (...versions) => versions.map(version => ({key: installerKey(version)}));
const feed = version => ({payload: Buffer.from(JSON.stringify({schema: 1, channel: 'tester', version,
  url: `https://downloads.callayers.com/${installerKey(version)}`})).toString('base64')});

function fixture(state, listed = objects(...[state.latestVersion, state.previousVersion].filter(Boolean))) {
  let record = structuredClone(state);
  const values = new Map([['tester/latest.json', feed(state.latestVersion)]]);
  const deleted = [], writes = [], logs = [];
  const store = {
    async readJson(key) { return key === RETENTION_KEY ? structuredClone(record) : values.get(key) ?? null; },
    async listInstallers() { return listed; },
    async deleteInstaller(version) { deleted.push(version); },
    async writeRetention(next) { record = structuredClone(next); writes.push(next); }
  };
  return {store, deleted, writes, values, logs, log: message => logs.push(message)};
}

test('first release and second release have no deletions', () => {
  const first = prepareRetention(null, '0.3.0', null, objects('0.3.0'), start);
  assert.deepEqual(first, {schema: 1, latestVersion: '0.3.0', previousVersion: null, retired: []});
  const second = prepareRetention(first, '0.4.0', '0.3.0', objects('0.3.0', '0.4.0'), start);
  assert.equal(second.previousVersion, '0.3.0');
  assert.deepEqual(second.retired, []);
});

test('third release retires the former previous version for a full day', () => {
  const second = {schema: 1, latestVersion: '0.4.0', previousVersion: '0.3.0', retired: []};
  const third = prepareRetention(second, '0.5.0', '0.4.0', objects('0.3.0', '0.4.0', '0.5.0'), start);
  assert.deepEqual(third, {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start + GRACE_MS).toISOString()}]});
  assert.deepEqual(prepareRetention(third, '0.5.0', '0.5.0', objects('0.3.0', '0.4.0', '0.5.0'), start + 1000), third);
});

test('failed feed publication restarts the retirement grace period on retry', () => {
  const prepared = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start + GRACE_MS).toISOString()}]};
  const retried = prepareRetention(prepared, '0.5.0', '0.4.0', objects('0.3.0', '0.4.0', '0.5.0'), start + GRACE_MS);
  assert.equal(Date.parse(retried.retired[0].deleteAfter), start + 2 * GRACE_MS);
});

test('only installer keys with matching numeric versions are retirement candidates', () => {
  const state = prepareRetention(null, '0.10.0', '0.9.0', [
    ...objects('0.8.0', '0.9.0', '0.10.0', '0.11.0'),
    {key: 'tester/latest.json'}, {key: 'releases/0.8.0/CallayersSetup-0.7.0.exe'},
    {key: 'releases/0.8.0/readme.txt'}, {key: 'releases/../CallayersSetup-...exe'}
  ], start);
  assert.deepEqual(state.retired.map(entry => entry.version), ['0.8.0']);
  assert(compareVersions('0.10.0', '0.9.0') > 0);
  assert.throws(() => prepareRetention(state, '0.9.0', '0.10.0', [], start), /newer/);
});

test('cleanup waits for the deadline, then deletes only the retired installer', async () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start + GRACE_MS).toISOString()}]};
  const bucket = fixture(state, [...objects('0.3.0', '0.4.0', '0.5.0'), {key: 'other-file.txt'}]);
  await cleanupReleases(bucket.store, {now: start + GRACE_MS - 1, log: bucket.log});
  assert.deepEqual(bucket.deleted, []);
  assert.deepEqual(bucket.writes, []);
  await cleanupReleases(bucket.store, {now: start + GRACE_MS, log: bucket.log});
  assert.deepEqual(bucket.deleted, ['0.3.0']);
  assert.deepEqual(bucket.writes[0].retired, []);
  assert.equal(bucket.writes[0].previousVersion, '0.4.0');
});

test('dry run shows the removal without any writes or deletes', async () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start).toISOString()}]};
  const bucket = fixture(state);
  await cleanupReleases(bucket.store, {dryRun: true, now: start, log: bucket.log});
  assert.deepEqual(bucket.deleted, []);
  assert.deepEqual(bucket.writes, []);
  assert(bucket.logs.some(message => message.includes('Would delete releases/0.3.0/')));
});

test('feed disagreement and a missing protected installer stop all cleanup', async () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0', retired: []};
  const mismatch = fixture(state);
  mismatch.values.set('tester/latest.json', feed('0.4.0'));
  await assert.rejects(cleanupReleases(mismatch.store), /disagree/);
  const missing = fixture(state, objects('0.5.0'));
  await assert.rejects(cleanupReleases(missing.store), /protected installer/);
  assert.deepEqual(mismatch.deleted, []);
  assert.deepEqual(missing.deleted, []);
});

test('malformed retention cannot target protected versions or paths', () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0', retired: []};
  for (const version of ['0.5.0', '0.4.0', '0.6.0', '../other', '65536.0.0']) {
    assert.throws(() => validateRetention({...state, retired: [{version, deleteAfter: new Date(start).toISOString()}]}));
  }
  assert.throws(() => validateRetention({...state, retired: [{version: '0.3.0', deleteAfter: 'invalid'}]}));
});

test('metadata changed during cleanup stops deletion and does not overwrite state', async () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start).toISOString()}]};
  const bucket = fixture(state);
  const originalList = bucket.store.listInstallers;
  bucket.store.listInstallers = async () => {
    bucket.values.set('tester/latest.json', feed('0.6.0'));
    return originalList();
  };
  await assert.rejects(cleanupReleases(bucket.store, {now: start, log: bucket.log}), /changed/);
  assert.deepEqual(bucket.deleted, []);
  assert.deepEqual(bucket.writes, []);
});

test('deletion failure keeps the retirement record available for retry', async () => {
  const state = {schema: 1, latestVersion: '0.5.0', previousVersion: '0.4.0',
    retired: [{version: '0.3.0', deleteAfter: new Date(start).toISOString()}]};
  const bucket = fixture(state);
  bucket.store.deleteInstaller = async () => { throw Error('simulated deletion failure'); };
  await assert.rejects(cleanupReleases(bucket.store, {now: start, log: bucket.log}), /deletion failure/);
  assert.deepEqual(bucket.writes, []);
});

test('an unpublished bucket is a no-op', async () => {
  await cleanupReleases({async readJson() {return null;}}, {log() {}});
});

test('a single published release remains protected without requiring a previous release', async () => {
  const bucket = fixture({schema: 1, latestVersion: '0.3.0', previousVersion: null, retired: []});
  await cleanupReleases(bucket.store, {now: start, log: bucket.log});
  assert.deepEqual(bucket.deleted, []);
  assert.deepEqual(bucket.writes, []);
});

test('Cloudflare listing follows pagination and DELETE targets an exact installer', async () => {
  const calls = [];
  const store = createReleaseStore({token: 'test-token', account: 'a'.repeat(32),
    fetchImpl: async (url, options) => {
      const parsed = new URL(url);
      calls.push({url: parsed, options});
      if (options.method === 'DELETE') return Response.json({success: true});
      const secondPage = parsed.searchParams.has('cursor');
      return Response.json({success: true, result: objects(secondPage ? '0.4.0' : '0.3.0'),
        result_info: {is_truncated: !secondPage, cursor: secondPage ? undefined : 'next/page'}});
    }});
  assert.deepEqual(await store.listInstallers(), objects('0.3.0', '0.4.0'));
  assert.equal(calls[1].url.searchParams.get('cursor'), 'next/page');
  await store.deleteInstaller('0.3.0');
  assert(calls[2].url.pathname.endsWith('/objects/releases/0.3.0/CallayersSetup-0.3.0.exe'));
  assert.equal(calls[2].options.method, 'DELETE');
  await assert.rejects(store.deleteInstaller('../unrelated'));
  assert.equal(calls.length, 3);
});

test('Cloudflare errors and incomplete listings fail closed', async () => {
  for (const response of [Response.json({success: false}), Response.json({success: true, result: [],
    result_info: {is_truncated: true}}), new Response('', {status: 403})]) {
    const store = createReleaseStore({token: 'test-token', account: 'a'.repeat(32), fetchImpl: async () => response});
    await assert.rejects(store.listInstallers());
  }
});
