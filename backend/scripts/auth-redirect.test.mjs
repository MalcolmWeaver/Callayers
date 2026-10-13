import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { build } from 'esbuild';
import Fastify from 'fastify';
import { registerAuthProxy } from '../dist/auth-proxy.js';

const state = 'a'.repeat(64);
const bundle = await build({ entryPoints: ['web/app.ts'], bundle: true, write: false, format: 'iife', plugins: [{
  name: 'mock-firebase', setup(b) {
    b.onResolve({ filter: /^firebase\// }, args => ({ path: args.path, namespace: 'mock' }));
    b.onLoad({ filter: /.*/, namespace: 'mock' }, () => ({ contents: `export const { initializeApp, getAuth, GoogleAuthProvider, signInWithRedirect, getRedirectResult, signOut, setPersistence, browserSessionPersistence } = globalThis.firebase;` }));
  }
}] });
async function browser({ hash = '', pending, credential = null, failure = false } = {}) {
  const elements = new Map(); const storage = new Map(pending ? [['callayers.google-redirect', JSON.stringify(pending)]] : []);
  const calls = []; let submitted;
  const context = {
    URL, URLSearchParams, Date, JSON, console,
    location: { hash, pathname: '/login.html' }, history: { replaceState() {} },
    sessionStorage: { getItem: k => storage.get(k), setItem: (k,v) => storage.set(k,v), removeItem: k => storage.delete(k) },
    document: {
      body: { appendChild() {} },
      createElement: () => ({ appendChild(input) { this.input = input; }, submit() { submitted = this; calls.push('submit'); } }),
      getElementById: name => {
        if (!elements.has(name)) elements.set(name, { hidden: false, click() { this.onclick?.(); } });
        return elements.get(name);
      }
    },
    fetch: async url => { calls.push(url); return { ok: true, json: async () => ({}) }; },
    firebase: {
      initializeApp: x => x, getAuth: () => ({}), GoogleAuthProvider: class { setCustomParameters() {} },
      signInWithRedirect: async () => { calls.push('redirect'); },
      getRedirectResult: async () => { if (failure) throw new Error('Google canceled'); return credential; },
      signOut: async () => { calls.push('signout'); }, setPersistence: async () => {}, browserSessionPersistence: {}
    }
  };
  vm.runInNewContext(bundle.outputFiles[0].text, context);
  await new Promise(resolve => setImmediate(resolve));
  return { calls, storage, elements, submitted };
}
const pending = { port: 49152, state, started: Date.now(), redirecting: true };
const credential = { user: { refreshToken: 'private-refresh-credential' } };
test('new desktop link waits for a deliberate Google click and saves tab state', async () => {
  const r = await browser({ hash: '#port=49152&state=' + state });
  assert.ok(!r.calls.includes('redirect'));
  assert.equal(r.elements.get('web-options').hidden, true);
  r.elements.get('signin').click();
  await new Promise(resolve => setImmediate(resolve));
  assert.ok(r.calls.includes('redirect'));
  assert.equal(JSON.parse(r.storage.get('callayers.google-redirect')).redirecting, true);
  assert.equal(r.submitted, undefined);
});
test('a direct website visit offers the email route without starting desktop sign-in', async () => {
  const r = await browser();
  assert.equal(r.elements.get('signin').hidden, true);
  assert.equal(r.elements.get('web-options').hidden, false);
  assert.equal(r.submitted, undefined);
  assert.ok(!r.calls.includes('redirect'));
});
test('fresh result posts credential only to loopback, clearing browser auth first', async () => {
  const r = await browser({ pending, credential });
  assert.equal(r.submitted.action, `http://127.0.0.1:49152/callback?state=${state}`);
  assert.equal(r.submitted.method, 'POST');
  assert.equal(r.submitted.input.name, 'refresh_token');
  assert.equal(r.submitted.input.value, credential.user.refreshToken);
  assert.deepEqual(r.calls, ['/v1/client-config', 'signout', 'submit']);
  assert.equal(r.storage.size, 0);
});
test('remembered account without fresh redirect never connects or loops', async () => {
  const r = await browser({ pending });
  assert.deepEqual(r.calls, ['/v1/client-config']);
  assert.equal(r.submitted, undefined);
  assert.match(r.elements.get('status').textContent, /did not finish/);
});
test('new link cannot consume an older Google result', async () => {
  const r = await browser({ hash: '#port=49152&state=' + state, pending, credential });
  assert.equal(r.submitted, undefined);
});
test('expired, malformed and canceled attempts never transfer credentials', async () => {
  for (const options of [
    { pending: { ...pending, started: Date.now() - 700000 }, credential },
    { pending: { ...pending, port: 80 }, credential },
    { pending: { ...pending, port: 65536 }, credential },
    { pending: { ...pending, port: 49152.5 }, credential },
    { pending: { ...pending, state: '../bad' }, credential },
    { pending: { ...pending, started: Date.now() + 700000 }, credential },
    { pending, failure: true }, { pending, credential: { user: {} } }
  ]) {
    const r = await browser(options);
    assert.equal(r.submitted, undefined);
    assert.ok(!r.calls.includes('redirect'));
  }
});
test('Firebase helper proxy pins upstream and strips desktop credentials', async () => {
  const app = Fastify(); const calls = [];
  await registerAuthProxy(app, 'callayers', async (url, options) => {
    calls.push({ url, options }); return new Response('helper', { headers: { 'Content-Type': 'text/html' } });
  });
  try {
    const r = await app.inject({ url: '/__/auth/handler?state=opaque', headers: { authorization: 'Bearer private', cookie: 'private=1' } });
    assert.equal(r.body, 'helper');
    assert.equal(calls[0].url, 'https://callayers.firebaseapp.com/__/auth/handler?state=opaque');
    assert.deepEqual(calls[0].options.headers, {});
    const post = await app.inject({ method: 'POST', url: '/__/auth/handler', headers: { 'content-type': 'application/x-www-form-urlencoded' }, payload: 'state=abc' });
    assert.equal(post.statusCode, 200);
    assert.equal(calls[1].options.body.toString(), 'state=abc');
    assert.equal((await app.inject('/__/auth/https://evil.example')).statusCode, 404);
    assert.equal(calls.length, 2);
  } finally { await app.close(); }
});
