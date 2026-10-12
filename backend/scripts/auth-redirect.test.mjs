import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { build } from 'esbuild';
import Fastify from 'fastify';
import { registerAuthProxy } from '../dist/auth-proxy.js';

const id = 'a'.repeat(64);
const bundle = await build({ entryPoints: ['web/app.ts'], bundle: true, write: false, format: 'iife', plugins: [{
  name: 'mock-firebase', setup(b) {
    b.onResolve({ filter: /^firebase\// }, args => ({ path: args.path, namespace: 'mock' }));
    b.onLoad({ filter: /.*/, namespace: 'mock' }, () => ({ contents: `export const { initializeApp, getAuth, GoogleAuthProvider, signInWithRedirect, getRedirectResult, signOut, onAuthStateChanged, setPersistence, browserSessionPersistence } = globalThis.firebase;` }));
  }
}] });
async function browser({ hash = '', pending, credential = null, failure = false } = {}) {
  const elements = new Map(); const storage = new Map(pending ? [['callayers.google-redirect', JSON.stringify(pending)]] : []);
  const calls = []; let navigated;
  const auth = { currentUser: { getIdToken: async () => 'token' } };
  const context = {
    URL, Date, JSON, console,
    location: { hash, pathname: '/login.html', replace: url => { navigated = url; } },
    history: { replaceState() {} },
    sessionStorage: { getItem: k => storage.get(k), setItem: (k,v) => storage.set(k,v), removeItem: k => storage.delete(k) },
    document: { getElementById: name => {
      if (!elements.has(name)) elements.set(name, { hidden: false, click() { this.onclick?.(); } });
      return elements.get(name);
    } },
    fetch: async url => { calls.push(url); return { ok: true, json: async () => url.endsWith('approve') ? { returnUrl: 'http://127.0.0.1:49152/callback?state=x&grant=y' } : {} }; },
    firebase: {
      initializeApp: x => x, getAuth: () => auth, GoogleAuthProvider: class { setCustomParameters() {} },
      signInWithRedirect: async () => { calls.push('redirect'); },
      getRedirectResult: async () => { if (failure) throw new Error('Google canceled'); return credential; },
      signOut: async () => {}, onAuthStateChanged: (_a, cb) => cb(auth.currentUser),
      setPersistence: async () => {}, browserSessionPersistence: {}
    }
  };
  vm.runInNewContext(bundle.outputFiles[0].text, context);
  await new Promise(resolve => setImmediate(resolve));
  return { calls, storage, elements, navigated };
}
const pending = { id, automatic: true, started: Date.now(), redirecting: true };
test('new desktop link redirects without a second click and saves tab state', async () => {
  const r = await browser({ hash: '#' + id + '&desktop=1' });
  assert.ok(r.calls.includes('redirect'));
  assert.equal(JSON.parse(r.storage.get('callayers.google-redirect')).redirecting, true);
  assert.ok(!r.calls.includes('/v1/login/approve'));
});
test('fresh Google redirect result approves and returns to desktop', async () => {
  const r = await browser({ pending, credential: { user: {} } });
  assert.ok(r.calls.includes('/v1/login/approve'));
  assert.match(r.navigated, /^http:\/\/127\.0\.0\.1:/);
  assert.equal(r.storage.size, 0);
});
test('remembered account without redirect result never approves or loops', async () => {
  const r = await browser({ pending });
  assert.deepEqual(r.calls, ['/v1/client-config']);
  assert.match(r.elements.get('status').textContent, /did not finish/);
});
test('new link cannot consume an older Google result', async () => {
  const r = await browser({ hash: '#' + id + '&desktop=1', pending, credential: {} });
  assert.ok(!r.calls.includes('/v1/login/approve'));
});
test('expired connection and provider failure do not approve', async () => {
  for (const options of [{ pending: { ...pending, started: Date.now() - 700000 }, credential: {} }, { pending, failure: true }]) {
    const r = await browser(options);
    assert.ok(!r.calls.includes('/v1/login/approve'));
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
