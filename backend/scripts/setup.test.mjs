import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { build } from 'esbuild';

const bundle = await build({ entryPoints: ['src/setup.ts'], bundle: true, write: false, format: 'iife', globalName: 'setup', plugins: [{
  name: 'firestore', setup(b) {
    b.onResolve({ filter: /^\.\/core\.js$/ }, args => args.importer.replaceAll('\\', '/').endsWith('/src/setup.ts') ? { path: 'core', namespace: 'mock' } : undefined);
    b.onLoad({ filter: /.*/, namespace: 'mock' }, () => ({ contents: 'export const {db,accountRef,ApiError} = globalThis.services;' }));
  }
}] });
function fixture() {
  const accounts = new Map([['alice', { disabled: false }], ['bob', { disabled: false }]]);
  const context = { services: { accountRef: uid => uid,
    ApiError: class extends Error { constructor(status, code, message) { super(message); this.code = code; } },
    db: { runTransaction: fn => fn({ get: async uid => ({ exists: accounts.has(uid), data: () => accounts.get(uid) }),
      update: (uid, change) => accounts.set(uid, { ...accounts.get(uid), ...change }) }) } } };
  vm.runInNewContext(bundle.outputFiles[0].text, context);
  return { accounts, ...context.setup };
}
test('new and legacy accounts start incomplete without importing local flags', () => {
  const f = fixture();
  assert.equal(f.readSetup(undefined).completed, false);
  assert.equal(f.readSetup(undefined).step, 0);
});
test('progress follows the authenticated account and completion survives stale writes', async () => {
  const f = fixture(), progress = { ...f.emptySetup(), step: 1, name: 'Alex', detail: 'Designer' };
  await f.saveSetup('alice', progress);
  assert.equal(f.readSetup(f.accounts.get('alice').setup).name, 'Alex');
  assert.equal(f.readSetup(f.accounts.get('bob').setup).name, '');
  await f.saveSetup('alice', { ...progress, step: 2, completed: true });
  await f.saveSetup('alice', progress);
  assert.equal(f.accounts.get('alice').setup.completed, true);
  assert.equal(f.accounts.get('alice').setup.step, 2);
});
test('setup rejects another account ID, camera identifiers, invalid steps and oversized text', async () => {
  const f = fixture();
  for (const change of [{ uid: 'bob' }, { camera: 1 }, { step: 3 }, { name: 'x'.repeat(61) }]) {
    await assert.rejects(() => f.saveSetup('alice', { ...f.emptySetup(), ...change }));
  }
  assert.equal(f.accounts.get('alice').setup, undefined);
});
test('disabled and missing accounts cannot save setup', async () => {
  const f = fixture(); f.accounts.get('alice').disabled = true;
  await assert.rejects(() => f.saveSetup('alice', f.emptySetup()), e => e.code === 'account_disabled');
  await assert.rejects(() => f.saveSetup('missing', f.emptySetup()), e => e.code === 'account_not_found');
});
