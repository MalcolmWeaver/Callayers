import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createHandler } from '../src/server.js';
import { accountStore } from '../src/accounts.js';
import { loadConfig } from '../src/config.js';

const config = { projectId: 'example-project', publicConfig: { version: 2, firebaseProjectId: 'example-project' } };
const identity = { uid: 'google-user', aud: config.projectId, iss: `https://securetoken.google.com/${config.projectId}`,
  email: 'example@example.com', email_verified: true, firebase: { sign_in_provider: 'google.com' } };
const enabled = { schemaVersion: 2, access: 'enabled', usageDate: '2026-10-13', dailyGenerations: 3 };
async function request(t, { identityResult = identity, authError, account = enabled, storeError, path = '/account',
  authorization = 'Bearer synthetic.token.value', method = 'GET' } = {}) {
  const calls = [], logs = [];
  const server = createServer(createHandler({ config, now: () => new Date('2026-10-13T00:01:00Z'), log: x => logs.push(x),
    auth: { async verifyIdToken(token, revoked) { calls.push(['verify', token, revoked]); if (authError) throw authError; return identityResult; } },
    accounts: { async getOrCreate(uid, date) { calls.push(['account', uid, date]); if (storeError) throw storeError; return account; } },
  }));
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); }));
  const headers = authorization === null ? {} : { Authorization: authorization };
  const response = await fetch(`http://127.0.0.1:${server.address().port}${path}`, { method, headers });
  return { status: response.status, body: await response.json(), calls, logs, cache: response.headers.get('cache-control') };
}
test('health and client config are public; unknown routes and methods do not reach authentication', async t => {
  for (const [path, method, expected] of [['/health', 'GET', 200], ['/client-config', 'GET', 200], ['/generate', 'POST', 405], ['/other', 'GET', 404]]) {
    const result = await request(t, { path, method }); assert.equal(result.status, expected); assert.deepEqual(result.calls, []);
    assert.equal(result.cache, 'no-store');
  }
});
test('missing or malformed credentials cannot access or create accounts', async t => {
  for (const authorization of [null, 'Basic synthetic', 'Bearer ', 'Bearer token with spaces']) {
    const result = await request(t, { authorization }); assert.equal(result.status, 401); assert.deepEqual(result.calls, []);
  }
});
test('verified Google owner gets account and current UTC usage; revocation is checked', async t => {
  const result = await request(t);
  assert.equal(result.status, 200); assert.equal(result.body.uid, identity.uid); assert.equal(result.body.accessAllowed, true);
  assert.deepEqual(result.body.allowance, { date: '2026-10-13', used: 3, enabled: false, limit: null, remaining: null });
  assert.deepEqual(result.calls, [['verify', 'synthetic.token.value', true], ['account', identity.uid, '2026-10-13']]);
});
test('expired/revoked identities and disabled users never touch account data', async t => {
  for (const [code, status] of [['auth/id-token-revoked', 401], ['auth/id-token-expired', 401], ['auth/user-disabled', 403]]) {
    const result = await request(t, { authError: { code } }); assert.equal(result.status, status); assert.equal(result.calls.length, 1);
  }
});
test('wrong project, unsafe uid, non-Google and unverified accounts are rejected before storage', async t => {
  for (const [patch, status] of [[{ aud: 'other-project' }, 401], [{ uid: 'a/b' }, 401], [{ uid: '' }, 401],
    [{ firebase: { sign_in_provider: 'password' } }, 403], [{ email_verified: false }, 403]]) {
    const result = await request(t, { identityResult: { ...identity, ...patch } }); assert.equal(result.status, status); assert.equal(result.calls.length, 1);
  }
});
test('denied accounts stay denied; old-day usage is displayed as zero without rewriting stored data', async t => {
  assert.equal((await request(t, { account: { ...enabled, access: 'disabled' } })).status, 403);
  const old = { ...enabled, usageDate: '2026-10-12' };
  const result = await request(t, { account: old }); assert.equal(result.body.allowance.used, 0); assert.equal(old.dailyGenerations, 3);
});
test('provider/storage outages return temporary errors and never leak private exception messages', async t => {
  for (const options of [{ authError: { code: 'auth/internal-error', message: 'PRIVATE_TOKEN' } }, { storeError: new Error('PRIVATE_PROFILE') },
    { account: { ...enabled, dailyGenerations: -1 } }]) {
    const result = await request(t, options); assert.equal(result.status, 503);
    assert.deepEqual(result.body, { error: 'temporarily_unavailable' }); assert.deepEqual(result.logs, ['account_service_unavailable']);
  }
});
test('account transaction creates once and preserves existing access/usage', async () => {
  const records = new Map(); let tail = Promise.resolve(), creates = 0;
  const db = { collection(name) { assert.equal(name, 'accounts'); return { doc(uid) { return uid; } }; },
    runTransaction(callback) { const work = tail.then(() => callback({
      async get(ref) { return { exists: records.has(ref), data: () => records.get(ref) }; },
      create(ref, value) { assert.equal(records.has(ref), false); records.set(ref, value); ++creates; },
    })); tail = work.catch(() => {}); return work; },
  };
  const store = accountStore(db, () => 'synthetic-time');
  const accounts = await Promise.all(Array.from({ length: 10 }, () => store.getOrCreate('one-user', '2026-10-13')));
  assert.equal(creates, 1); assert.equal(records.size, 1); assert.ok(accounts.every(x => x === accounts[0]));
  records.get('one-user').access = 'disabled'; records.get('one-user').dailyGenerations = 5;
  assert.equal((await store.getOrCreate('one-user', '2026-10-14')).access, 'disabled'); assert.equal(creates, 1);
  assert.equal(records.get('one-user').dailyGenerations, 5);
});
test('configuration requires separate database and installed OAuth client in the Firebase project', () => {
  const env = { GOOGLE_CLOUD_PROJECT: 'example-project', FIRESTORE_DATABASE_ID: 'callayers-v2', FIREBASE_WEB_API_KEY: 'synthetic-key',
    GOOGLE_DESKTOP_CLIENT_JSON: JSON.stringify({ installed: { project_id: 'example-project', client_id: 'synthetic.apps.googleusercontent.com', client_secret: 'synthetic-installed-client' } }) };
  assert.equal(loadConfig(env).publicConfig.version, 2);
  assert.throws(() => loadConfig({ ...env, FIRESTORE_DATABASE_ID: '(default)' }));
  assert.throws(() => loadConfig({ ...env, GOOGLE_CLOUD_PROJECT: 'other-project' }));
  assert.throws(() => loadConfig({ ...env, GOOGLE_DESKTOP_CLIENT_JSON: JSON.stringify({ web: {} }) }));
});
