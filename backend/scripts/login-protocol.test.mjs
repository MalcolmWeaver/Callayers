import test from 'node:test';
import assert from 'node:assert/strict';
import { startLoginBody, exchangeLoginBody, desktopReturnUrl } from '../dist/login-protocol.js';

const secret = 'a'.repeat(64);
test('desktop return allows only an ephemeral loopback port and a strong state', () => {
  const target = { port: 49152, state: secret };
  assert.equal(desktopReturnUrl(target, 'b'.repeat(64)), `http://127.0.0.1:49152/callback?state=${secret}&grant=${'b'.repeat(64)}`);
  for (const desktopReturn of [
    { ...target, port: 0 }, { ...target, port: 80 }, { ...target, port: 65536 },
    { ...target, port: 49152.5 }, { ...target, state: 'short' },
    { ...target, host: 'attacker.example' }, { ...target, url: 'https://attacker.example' }
  ]) assert.equal(startLoginBody.safeParse({ challenge: secret, desktopReturn }).success, false);
});
test('exchange rejects malformed grants and keeps existing manual clients compatible', () => {
  assert.equal(exchangeLoginBody.safeParse({ id: secret, proof: secret }).success, true);
  assert.equal(exchangeLoginBody.safeParse({ id: secret, proof: secret, grant: 'b'.repeat(64) }).success, true);
  for (const grant of ['', 'short', 'g'.repeat(64), '../callback'])
    assert.equal(exchangeLoginBody.safeParse({ id: secret, proof: secret, grant }).success, false);
});
