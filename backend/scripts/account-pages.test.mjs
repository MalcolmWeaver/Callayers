import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { validateSignIn, validateSignUp } from '../web/validation.ts';
import { submitSignIn, submitSignUp } from '../web/account-flow.ts';

const html = name => readFileSync(new URL(`../web/${name}.html`, import.meta.url), 'utf8');

test('sign-in and sign-up pages include accessible fields and cross-links', () => {
  for (const [page, fields, target] of [
    ['signin', { email: 'email', password: 'current-password' }, '/signup'],
    ['signup', { name: 'name', email: 'email', password: 'new-password', confirmPassword: 'new-password' }, '/signin']
  ]) {
    const source = html(page);
    assert.match(source, new RegExp(`href="${target}"`));
    assert.match(source, /id="google-signin"[^>]+type="button"/);
    assert.match(source, /href="\/auth\.css"/);
    assert.doesNotMatch(source, /twitch/i);
    assert.match(source, new RegExp(`<form[^>]+method="post"[^>]+action="/${page}"`));
    assert.match(source, /<button class="primary-button" type="submit">/);
    for (const [name, autocomplete] of Object.entries(fields)) {
      assert.match(source, new RegExp(`<label for="${name}">`));
      assert.match(source, new RegExp(`<input id="${name}" name="${name}"[^>]+autocomplete="${autocomplete}"[^>]+aria-invalid="false"[^>]+aria-describedby="${name}-error"`));
      assert.match(source, new RegExp(`id="${name}-error"`));
    }
  }
  assert.match(html('home'), /id="signout"/);
  assert.match(html('login'), /id="web-options"/);
  assert.doesNotMatch(html('login'), /twitch/i);
});

test('validation reports required, format, length and confirmation errors', () => {
  assert.deepEqual(validateSignIn({ email: '', password: '' }), {
    email: 'Email is required.', password: 'Password is required.'
  });
  assert.deepEqual(validateSignIn({ email: 'ada@example.com', password: 'short6' }), {});
  assert.deepEqual(validateSignUp({ name: ' ', email: 'bad', password: 'short', confirmPassword: 'other' }), {
    name: 'Name is required.', email: 'Enter a valid email address.',
    password: 'Password must be at least 8 characters.', confirmPassword: 'Passwords do not match.'
  });
  assert.deepEqual(validateSignUp({ name: 'Ada', email: 'ada@example.com', password: 'strongpass', confirmPassword: '' }), {
    confirmPassword: 'Confirm your password.'
  });
});

test('invalid submissions never call the auth service', async () => {
  let called = 0;
  const action = async () => { called++; };
  assert.equal((await submitSignIn({ email: 'bad', password: 'short' }, action)).ok, false);
  assert.equal((await submitSignUp({ name: '', email: '', password: '', confirmPassword: '' }, action)).ok, false);
  assert.equal(called, 0);
});

test('existing six-character passwords can reach the sign-in service', async () => {
  let password;
  assert.deepEqual(await submitSignIn({ email: 'ada@example.com', password: 'sixsix' }, async fields => { password = fields.password; }), { ok: true });
  assert.equal(password, 'sixsix');
});

test('valid submissions call the auth service with trimmed account fields', async () => {
  const calls = [];
  assert.deepEqual(await submitSignIn({ email: ' ada@example.com ', password: 'strongpass' }, async fields => { calls.push(fields); }), { ok: true });
  assert.deepEqual(await submitSignUp({ name: ' Ada ', email: ' ada@example.com ', password: 'strongpass', confirmPassword: 'strongpass' }, async fields => { calls.push(fields); }), { ok: true });
  assert.deepEqual(calls, [
    { email: 'ada@example.com', password: 'strongpass' },
    { name: 'Ada', email: 'ada@example.com', password: 'strongpass' }
  ]);
});

test('service failures show useful inline errors', async () => {
  const valid = { email: 'ada@example.com', password: 'strongpass' };
  const invalidCredentials = await submitSignIn(valid, async () => { throw { code: 'INVALID_CREDENTIALS' }; });
  assert.equal(invalidCredentials.formError, 'Invalid email or password.');
  const taken = await submitSignUp({ ...valid, name: 'Ada', confirmPassword: 'strongpass' }, async () => { throw { code: 'EMAIL_TAKEN' }; });
  assert.equal(taken.fields.email, 'Email already registered.');
});
