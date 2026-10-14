import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { accountStore } from '../src/accounts.js';
import { createHandler } from '../src/server.js';
import { newTicket, ticketId, WEBSITE_ORIGIN } from '../src/attribution.js';

function store() {
  const records = new Map([['accounts/owner', { schemaVersion: 2, access: 'enabled' }]]);
  let tail = Promise.resolve();
  const db = {
    collection: name => ({ doc: id => ({ key: `${name}/${id}`, async create(value) {
      assert(!records.has(this.key)); records.set(this.key, value);
    } }) }),
    runTransaction(callback) {
      const work = tail.then(() => callback({
        async get(ref) { return { exists: records.has(ref.key), data: () => records.get(ref.key) }; },
        delete(ref) { records.delete(ref.key); },
      })); tail = work.catch(() => {}); return work;
    },
  };
  return { accounts: accountStore(db, () => new Date()), records };
}

test('tickets are opaque, hashed at rest, expiring and consumed atomically across instances', async () => {
  const { accounts, records } = store(), ticket = newTicket(), now = new Date();
  await accounts.createAttributionTicket('owner', ticket, new Date(now.getTime() + 120000));
  assert.match(ticket, /^[A-Za-z0-9_-]{43}$/);
  assert(![...records.keys()].some(key => key.includes(ticket)));
  const results = await Promise.all(Array.from({ length: 5 }, () => accounts.consumeAttributionTicket(ticket, now)));
  assert.equal(results.filter(x => x === 'owner').length, 1);
  assert(!records.has(`requests/${ticketId(ticket)}`));
  const expired = newTicket();
  await accounts.createAttributionTicket('owner', expired, now);
  assert.equal(await accounts.consumeAttributionTicket(expired, now), null);
  const denied = newTicket();
  await accounts.createAttributionTicket('owner', denied, new Date(now.getTime() + 120000));
  records.get('accounts/owner').access = 'disabled';
  assert.equal(await accounts.consumeAttributionTicket(denied, now), null);
});

test('only verified enabled desktop accounts mint links; only the website redeems them', async t => {
  const { accounts } = store();
  const config = { projectId: 'test', posthogToken: 'synthetic_public_capture_key', posthogHost: 'https://us.i.posthog.com' };
  const server = createServer(createHandler({ config, accounts,
    auth: { async verifyIdToken(token) {
      if (token !== 'valid') throw { code: 'auth/invalid-id-token' };
      return { uid: 'owner', aud: 'test', iss: 'https://securetoken.google.com/test',
        email_verified: true, firebase: { sign_in_provider: 'google.com' } };
    } },
  }));
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());
  const url = `http://127.0.0.1:${server.address().port}`;
  const mint = authorization => fetch(`${url}/attribution/ticket`, { method: 'POST', headers: authorization ? { Authorization: authorization } : {} });
  assert.equal((await mint()).status, 401);
  assert.equal((await mint('Bearer forged')).status, 401);
  const minted = await mint('Bearer valid'); assert.equal(minted.status, 200);
  const { ticket } = await minted.json();
  const consume = (origin, body = { ticket }) => fetch(`${url}/attribution/consume`, {
    method: 'POST', headers: { Origin: origin, 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  assert.equal((await consume('https://evil.example')).status, 403);
  assert.equal((await consume(WEBSITE_ORIGIN, { ticket, uid: 'forged' })).status, 400);
  const consumed = await consume(WEBSITE_ORIGIN);
  assert.equal(consumed.status, 200);
  assert.equal(consumed.headers.get('access-control-allow-origin'), WEBSITE_ORIGIN);
  assert.deepEqual(await consumed.json(), { distinctId: 'callayers:owner' });
  assert.equal((await consume(WEBSITE_ORIGIN)).status, 410);
  assert.equal((await consume(WEBSITE_ORIGIN, { ticket: 'a'.repeat(43) })).status, 410);
  const preflight = await fetch(`${url}/attribution/consume`, { method: 'OPTIONS', headers: { Origin: WEBSITE_ORIGIN } });
  assert.equal(preflight.status, 204);
  assert.equal(preflight.headers.get('access-control-allow-methods'), 'POST');
  const publicConfig = await fetch(`${url}/web-analytics-config`, { headers: { Origin: WEBSITE_ORIGIN } });
  assert.deepEqual(await publicConfig.json(), { token: config.posthogToken, host: config.posthogHost });
  const authCors = await fetch(`${url}/account`, { method: 'OPTIONS', headers: { Origin: WEBSITE_ORIGIN } });
  assert.equal(authCors.headers.get('access-control-allow-origin'), null);
});
