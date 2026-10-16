import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
import {RELEASE_BUCKET, RELEASE_BASE_URL} from '../scripts/release-storage.mjs';
const script = await readFile(new URL('../website/public/download.js', import.meta.url), 'utf8');

async function show(url, published = true, failure = null) {
  const link = {hidden: true}, status = {textContent: 'No release yet'}, requests = [];
  await runInNewContext(script, {document: {getElementById: id => ({'installer-link': link, 'release-status': status})[id] ?? null},
    Uint8Array, TextDecoder, AbortSignal, atob,
    fetch: async address => {
      requests.push(address);
      if (failure === 'network') throw Error('offline');
      if (address === '/download-config.json') return Response.json({bucket: RELEASE_BUCKET, baseUrl: RELEASE_BASE_URL});
      if (failure === 'server') return new Response(null,{status:503});
      if (!published) return new Response(null, {status: 404});
      return Response.json({payload: Buffer.from(JSON.stringify({schema: 1, channel: 'tester', version: '0.3.0', url})).toString('base64')});
    }});
  return {link, status, requests};
}

test('website offers the exact published Cloudflare installer', async () => {
  const url = `${RELEASE_BASE_URL}/releases/0.3.0/CallayersSetup-0.3.0.exe`;
  const page = await show(url);
  assert.equal(page.link.hidden, false);
  assert.equal(page.link.href, url);
  assert.equal(page.status.textContent,'Windows: 0.3.0 · x64 · Tester release');
  assert.deepEqual(page.requests, ['/download-config.json', `${RELEASE_BASE_URL}/tester/latest.json`]);
});

test('website rejects an installer on an unexpected storage host', async () => {
  const page = await show('https://storage.googleapis.com/other/releases/0.3.0/CallayersSetup-0.3.0.exe');
  assert.equal(page.link.hidden, true);
  assert.equal(page.link.href, undefined);
  assert.equal(page.status.textContent,'Windows: Release status unavailable. Try again shortly.');
});

test('website leaves downloads unavailable before the first publication', async () => {
  const page = await show('', false);
  assert.equal(page.link.hidden, true);
  assert.equal(page.status.textContent, 'Windows: Not published yet.');
});

test('website distinguishes a failed Windows lookup from an unpublished release',async()=>{
  for(const failure of ['network','server']) {
    const page=await show('',true,failure);
    assert.equal(page.link.hidden,true);
    assert.equal(page.status.textContent,'Windows: Release status unavailable. Try again shortly.');
  }
});
