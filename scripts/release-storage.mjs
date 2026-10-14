import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {randomUUID} from 'node:crypto';

export const RELEASE_BUCKET = JSON.parse(readFileSync(new URL('../release/download-config.json', import.meta.url), 'utf8')).bucket;
if (!/^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/.test(RELEASE_BUCKET)) throw Error('Invalid release bucket configuration.');
export const RELEASE_BASE_URL = `https://storage.googleapis.com/${RELEASE_BUCKET}`;

export function getStorageToken() {
  try {
    // Keep credentials in gcloud/GitHub's short-lived identity; never print the token.
    const token = process.platform === 'win32'
      ? execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        '& gcloud auth print-access-token; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }'], {encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']})
      : execFileSync('gcloud', ['auth', 'print-access-token'], {encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']});
    if (!token.trim()) throw Error();
    return token.trim();
  } catch { throw Error('Google authentication unavailable. Sign in with gcloud or configure the GitHub workload identity.'); }
}

export function createReleaseStore({token, fetchImpl = fetch}) {
  if (!token) throw Error('Google authentication is required.');
  const endpoint = `https://storage.googleapis.com/storage/v1/b/${RELEASE_BUCKET}/o`;
  const generations = new Map();
  const objectUrl = key => `${endpoint}/${encodeURIComponent(key)}`;
  async function request(url, options = {}) {
    return fetchImpl(url, {...options, redirect: 'error', signal: AbortSignal.timeout(300000),
      headers: {Authorization: `Bearer ${token}`, ...options.headers}});
  }
  async function checkedJson(response, operation) {
    if (!response.ok) throw Error(`Google Storage ${operation} failed (${response.status}); operation stopped.`);
    return response.json();
  }
  async function readBytes(key) {
    const metadataResponse = await request(objectUrl(key));
    if (metadataResponse.status === 404) { generations.set(key, '0'); return null; }
    const metadata = await checkedJson(metadataResponse, 'metadata read');
    if (!/^\d+$/.test(metadata.generation ?? '')) throw Error('Invalid object generation.');
    generations.set(key, metadata.generation);
    const url = new URL(objectUrl(key));
    url.searchParams.set('alt', 'media');
    url.searchParams.set('generation', metadata.generation);
    const response = await request(url);
    if (!response.ok) throw Error(`Google Storage object read failed (${response.status}).`);
    return Buffer.from(await response.arrayBuffer());
  }
  async function writeObject(key, bytes, contentType, cacheControl, {createOnly = false} = {}) {
    const generation = createOnly ? '0' : generations.get(key);
    if (generation === undefined) throw Error('Read an object before replacing it.');
    const boundary = `callayers-${randomUUID()}`;
    const metadata = JSON.stringify({name: key, contentType, cacheControl});
    const body = Buffer.concat([Buffer.from(`--${boundary}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n${metadata}\r\n--${boundary}\r\nContent-Type: ${contentType}\r\n\r\n`),
      Buffer.from(bytes), Buffer.from(`\r\n--${boundary}--\r\n`)]);
    const url = new URL(`https://storage.googleapis.com/upload/storage/v1/b/${RELEASE_BUCKET}/o`);
    url.searchParams.set('uploadType', 'multipart');
    url.searchParams.set('ifGenerationMatch', generation);
    const result = await checkedJson(await request(url, {method: 'POST',
      headers: {'Content-Type': `multipart/related; boundary=${boundary}`}, body}), 'upload');
    if (!/^\d+$/.test(result.generation ?? '')) throw Error('Invalid uploaded object generation.');
    generations.set(key, result.generation);
  }
  return {
    readBytes, writeObject,
    async readJson(key) {
      const bytes = await readBytes(key);
      return bytes === null ? null : JSON.parse(bytes.toString('utf8'));
    },
    async listInstallers() {
      const objects = [], seen = new Set();
      let pageToken;
      do {
        const url = new URL(endpoint);
        url.searchParams.set('prefix', 'releases/');
        url.searchParams.set('maxResults', '1000');
        if (pageToken) url.searchParams.set('pageToken', pageToken);
        const body = await checkedJson(await request(url), 'release listing');
        if (body.kind !== 'storage#objects' || (body.items !== undefined && !Array.isArray(body.items))) throw Error('Invalid Google Storage release listing.');
        for (const item of body.items ?? []) {
          if (typeof item.name !== 'string' || !/^\d+$/.test(item.generation ?? '')) throw Error('Invalid installer metadata.');
          generations.set(item.name, item.generation);
          objects.push({key: item.name, generation: item.generation});
        }
        pageToken = body.nextPageToken;
        if (pageToken !== undefined && (typeof pageToken !== 'string' || !pageToken || seen.has(pageToken))) throw Error('Incomplete Google Storage release listing.');
        seen.add(pageToken);
      } while (pageToken);
      return objects;
    },
    async writeRetention(state) {
      await writeObject('tester/retention.json', JSON.stringify(state), 'application/json', 'no-store');
    },
    async deleteObject(key) {
      const generation = generations.get(key);
      if (!generation || generation === '0') throw Error('List an installer before deleting it.');
      const url = new URL(objectUrl(key));
      url.searchParams.set('ifGenerationMatch', generation);
      const response = await request(url, {method: 'DELETE'});
      if (!response.ok && response.status !== 404) throw Error(`Google Storage installer deletion failed (${response.status}).`);
    }
  };
}
