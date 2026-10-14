import {readFileSync} from 'node:fs';
import {mkdtemp, writeFile, unlink, rmdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const config = JSON.parse(readFileSync(new URL('../release/download-config.json', import.meta.url), 'utf8'));
export const RELEASE_BUCKET = config.bucket;
export const RELEASE_BASE_URL = config.baseUrl;
if (RELEASE_BUCKET !== 'callayers-downloads' || RELEASE_BASE_URL !== 'https://downloads.callayers.com') throw Error('Invalid release storage configuration.');

export function getStorageToken() {
  if (!process.env.CLOUDFLARE_API_TOKEN) throw Error('Configure CLOUDFLARE_API_TOKEN for R2 publishing.');
  return process.env.CLOUDFLARE_API_TOKEN;
}

async function uploadWithWrangler(key, bytes, contentType, cacheControl, {token, account}) {
  const directory = await mkdtemp(path.join(tmpdir(), 'callayers-r2-upload-'));
  const file = path.join(directory, 'object.bin');
  try {
    await writeFile(file, bytes);
    const args = ['--yes', 'wrangler@4.77.0', 'r2', 'object', 'put', `${RELEASE_BUCKET}/${key}`,
      '--file', file, '--content-type', contentType, '--cache-control', cacheControl, '--remote'];
    const env = {...process.env, CLOUDFLARE_API_TOKEN: token, CLOUDFLARE_ACCOUNT_ID: account};
    const result = process.platform === 'win32'
      ? spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
        fileURLToPath(new URL('./upload-r2.ps1', import.meta.url)), '-Object', `${RELEASE_BUCKET}/${key}`,
        '-File', file, '-ContentType', contentType, '-CacheControl', cacheControl], {stdio: 'inherit', env})
      : spawnSync('npx', args, {stdio: 'inherit', env});
    if (result.status !== 0) throw Error('Cloudflare upload failed.');
  } finally {
    // Remove only the exact file and empty directory created by this invocation.
    await unlink(file).catch(error => {if (error.code !== 'ENOENT') throw error;});
    await rmdir(directory);
  }
}

export function createReleaseStore({token, account = process.env.CLOUDFLARE_ACCOUNT_ID, fetchImpl = fetch, uploadImpl = uploadWithWrangler}) {
  if (!token || !/^[a-f0-9]{32}$/.test(account ?? '')) throw Error('Configure Cloudflare account and API token.');
  const endpoint = `https://api.cloudflare.com/client/v4/accounts/${account}/r2/buckets/${RELEASE_BUCKET}/objects`;
  const objectUrl = key => `${endpoint}/${key.split('/').map(encodeURIComponent).join('/')}`;
  const allowedKey = key => /^(tester\/(latest|retention)\.json|releases\/(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\/CallayersSetup-\3\.\4\.\5\.exe)$/.test(key);
  const reads = new Map();
  async function request(url, options = {}) {
    return fetchImpl(url, {...options, redirect: 'error', signal: AbortSignal.timeout(120000),
      headers: {Authorization: `Bearer ${token}`, ...options.headers}});
  }
  async function checkedJson(response, operation) {
    if (!response.ok) throw Error(`Cloudflare ${operation} failed (${response.status}); operation stopped.`);
    const body = await response.json();
    if (body.success !== true) throw Error(`Cloudflare ${operation} failed; operation stopped.`);
    return body;
  }
  async function readBytes(key) {
    const response = await request(objectUrl(key));
    if (response.status === 404) {reads.set(key, null); return null;}
    if (!response.ok) throw Error(`Cloudflare object read failed (${response.status}).`);
    const bytes = Buffer.from(await response.arrayBuffer());
    reads.set(key, bytes);
    return bytes;
  }
  async function writeObject(key, bytes, contentType, cacheControl, {createOnly = false} = {}) {
    if (!allowedKey(key)) throw Error('Unexpected release object.');
    if (!reads.has(key) && !createOnly) throw Error('Read an object before replacing it.');
    // GitHub serializes all releases and cleanup. Detect changes made outside that workflow too.
    const expected = reads.get(key);
    const current = await readBytes(key);
    if (createOnly ? current !== null : expected === null ? current !== null : !current || !expected.equals(current)) {
      throw Error('Release object changed before upload; operation stopped.');
    }
    await uploadImpl(key, bytes, contentType, cacheControl, {token, account});
    reads.set(key, Buffer.from(bytes));
  }
  return {
    readBytes, writeObject,
    async readJson(key) {
      const bytes = await readBytes(key);
      return bytes === null ? null : JSON.parse(bytes.toString('utf8'));
    },
    async listInstallers() {
      const objects = [], seen = new Set();
      let cursor;
      do {
        const url = new URL(endpoint);
        url.searchParams.set('prefix', 'releases/');
        url.searchParams.set('per_page', '1000');
        if (cursor) url.searchParams.set('cursor', cursor);
        const body = await checkedJson(await request(url), 'release listing');
        if (!Array.isArray(body.result)) throw Error('Invalid Cloudflare release listing.');
        objects.push(...body.result);
        if (!body.result_info || typeof body.result_info.is_truncated !== 'boolean') throw Error('Invalid Cloudflare pagination.');
        cursor = body.result_info.is_truncated ? body.result_info.cursor : null;
        if (body.result_info.is_truncated && (!cursor || seen.has(cursor))) throw Error('Incomplete Cloudflare release listing.');
        seen.add(cursor);
      } while (cursor);
      return objects;
    },
    async writeRetention(state) {
      await writeObject('tester/retention.json', JSON.stringify(state), 'application/json', 'no-store');
    },
    async deleteObject(key) {
      if (!allowedKey(key) || !key.startsWith('releases/')) throw Error('Unexpected installer object.');
      const response = await request(objectUrl(key), {method: 'DELETE'});
      if (response.status !== 404) await checkedJson(response, 'installer deletion');
    }
  };
}
