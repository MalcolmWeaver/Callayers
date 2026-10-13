import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { build } from 'esbuild';

// Exercise the real reservation and settlement code without Firestore or AI calls.
const bundle = await build({ entryPoints: ['src/ai.ts'], bundle: true, write: false, format: 'iife', globalName: 'ai', define: { 'import.meta.url': '"file:///mock/ai.js"' }, plugins: [{
  name: 'services', setup(b) {
    b.onResolve({ filter: /^\.\/core\.js$/ }, args => (/\/src\/(ai|setup)\.ts$/).test(args.importer.replaceAll('\\', '/')) ? { path: 'core', namespace: 'mock' } : undefined);
    b.onResolve({ filter: /^node:fs$/ }, () => ({ path: 'fs', namespace: 'mock' }));
    b.onLoad({ filter: /.*/, namespace: 'mock' }, args => ({ contents: args.path === 'fs'
      ? 'export const readFileSync = () => "Card instructions";'
      : 'export const {db,config,month,micros,accountRef,hash,expires,ApiError} = globalThis.services;' }));
  }
}] });
async function scenario(overrides = {}, policy = {}) {
  const account = { email: 'test@example.com', approved: false, paid: false, disabled: false,
    trialStartedAt: null, trialEndsAt: null, trialSpent: 0, trialReserved: 0, totalSpent: 0, activeUntil: 0, lastRequestAt: 0, ...overrides };
  const records = new Map([['accounts/user', account], ['controls/ai', policy]]);
  const snapshot = path => ({ exists: records.has(path), data: () => records.get(path) });
  const ref = path => ({ path, get: async () => snapshot(path), collection: name => ({ doc: id => ref(path + '/' + name + '/' + id) }) });
  const set = (r, data) => records.set(r.path, data);
  const db = { collection: name => ({ doc: id => ref(name + '/' + id) }), doc: ref,
    runTransaction: fn => fn({ get: async r => snapshot(r.path), create: set, set, update: (r,d) => set(r,{...records.get(r.path),...d}) }) };
  let providerCalls = 0;
  const context = { URL: class { constructor() {} }, Buffer, AbortSignal, console,
    fetch: async () => { providerCalls++; return new Response(JSON.stringify({ status: 'completed', usage: { input_tokens: 10, output_tokens: 10 } })); },
    services: { db, accountRef: uid => ref('accounts/' + uid), month: () => '2026-10', micros: usd => usd * 1e6, hash: text => text,
      expires: ms => ({ toMillis: () => Date.now() + ms }), ApiError: class extends Error { constructor(status, code, message) { super(message); this.code = code; } },
      config: { TRIAL_DAYS: 7, TRIAL_USD: 1, PAID_MONTHLY_USD: 10, GLOBAL_MONTHLY_USD: 25, MAX_INPUT_BYTES: 120000,
        MAX_OUTPUT_TOKENS: 100, AI_ENABLED: 'true', OPENAI_API_KEY: 'mock', PRICE_VERSION: 'test', INPUT_USD_PER_MILLION: 1, OUTPUT_USD_PER_MILLION: 1 } } };
  vm.runInNewContext(bundle.outputFiles[0].text, context);
  return { records, providerCalls: () => providerCalls, status: () => context.ai.accountStatus('user'),
    generate: () => context.ai.generate('user', { requestId: '3caad148-b693-41d0-bda6-4a71485896bc', input: { task: 'Create a new card', prompt: 'Hello', width: 600, height: 200 } }) };
}
test('previously unapproved account can generate and starts its existing trial', async () => {
  const s = await scenario();
  assert.equal((await s.status()).approved, true);
  assert.equal((await s.generate()).state, 'completed');
  assert.equal(s.providerCalls(), 1);
  const account = s.records.get('accounts/user');
  assert.ok(account.trialStartedAt > 0);
  assert.equal(account.trialEndsAt - account.trialStartedAt, 7 * 86400000);
  assert.equal(account.trialSpent, 20);
  assert.equal(account.trialReserved, 0);
});
test('disabled accounts, expired trials, allowance and rate limits still block AI', async () => {
  for (const [overrides, code] of [
    [{ disabled: true }, 'account_disabled'], [{ trialEndsAt: 1 }, 'trial_expired'],
    [{ trialSpent: 1e6 }, 'allowance_exhausted'], [{ activeUntil: Date.now() + 60000 }, 'generation_in_progress'],
    [{ lastRequestAt: Date.now() }, 'generation_rate_limited']
  ]) {
    const s = await scenario(overrides);
    await assert.rejects(s.generate, e => e.code === code);
    assert.equal(s.providerCalls(), 0);
  }
});
test('service pause and global spending cap remain enforced', async () => {
  const paused = await scenario({}, { disabled: true });
  await assert.rejects(paused.generate, e => e.code === 'ai_paused');
  const capped = await scenario();
  capped.records.set('globalUsage/2026-10', { spent: 25e6, reserved: 0 });
  await assert.rejects(capped.generate, e => e.code === 'global_allowance_exhausted');
  assert.equal(capped.providerCalls(), 0);
});
test('existing trial dates and spending survive the removal of approval', async () => {
  const start = Date.now() - 86400000, end = start + 7 * 86400000;
  const s = await scenario({ trialStartedAt: start, trialEndsAt: end, trialSpent: 1000 });
  await s.generate();
  const account = s.records.get('accounts/user');
  assert.equal(account.trialStartedAt, start);
  assert.equal(account.trialEndsAt, end);
  assert.equal(account.trialSpent, 1020);
});
test('paid allowance still uses monthly spending and reports its reset', async () => {
  const s = await scenario({ paid: true, trialEndsAt: 1 });
  s.records.set('accounts/user/months/2026-10', { spent: 10e6, reserved: 0 });
  await assert.rejects(s.generate, e => e.code === 'allowance_exhausted' && /resets on \d{4}-\d{2}-01 \(UTC\)/.test(e.message));
  assert.equal(s.providerCalls(), 0);
});
