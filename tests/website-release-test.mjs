import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
import {RELEASE_BUCKET, RELEASE_BASE_URL} from '../scripts/release-storage.mjs';
const script = await readFile(new URL('../website/public/download.js', import.meta.url), 'utf8');

async function show(url, published = true) {
  const link = {hidden: true}, status = {textContent: 'No release yet'}, requests = [];
  await runInNewContext(script, {document: {getElementById: id => id === 'installer-link' ? link : status},
    Uint8Array, TextDecoder, AbortSignal, atob,
    fetch: async address => {
      requests.push(address);
      if (address === '/download-config.json') return Response.json({bucket: RELEASE_BUCKET});
      if (!published) return new Response(null, {status: 404});
      return Response.json({payload: Buffer.from(JSON.stringify({schema: 1, channel: 'tester', version: '0.3.0', url})).toString('base64')});
    }});
  return {link, status, requests};
}

test('website offers the exact published GCS installer', async () => {
  const url = `${RELEASE_BASE_URL}/releases/0.3.0/CallayersSetup-0.3.0.exe`;
  const page = await show(url);
  assert.equal(page.link.hidden, false);
  assert.equal(page.link.href, url);
  assert(page.status.textContent.includes('0.3.0'));
  assert.deepEqual(page.requests, ['/download-config.json', `${RELEASE_BASE_URL}/tester/latest.json`]);
});

test('website rejects an installer in another Google bucket', async () => {
  const page = await show('https://storage.googleapis.com/other/releases/0.3.0/CallayersSetup-0.3.0.exe');
  assert.equal(page.link.hidden, true);
  assert.equal(page.link.href, undefined);
});

test('website leaves downloads unavailable before the first publication', async () => {
  const page = await show('', false);
  assert.equal(page.link.hidden, true);
  assert.equal(page.status.textContent, 'No release yet');
});
