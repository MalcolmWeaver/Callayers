import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {incrementVersion, publishedVersion, selectVersion, checkCandidate, reportVersion} from '../scripts/release-version.mjs';
import {RELEASE_BASE_URL} from '../scripts/release-storage.mjs';

const windowsFeed = version => JSON.stringify({payload: Buffer.from(JSON.stringify({schema:1, channel:'tester', version,
  url:`${RELEASE_BASE_URL}/releases/${version}/CallayersSetup-${version}.exe`})).toString('base64'), signature:'test'});
const linuxFeed = version => `-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA256\n\nX-Callayers-Version: ${version}\n`;
const options = (platform, version) => ({fetchImpl:async (url, request) => {
  assert.equal(url, `${RELEASE_BASE_URL}/${platform === 'linux' ? 'linux/apt/dists/stable/InRelease' : 'tester/latest.json'}`);
  assert.equal(request.redirect,'error');
  assert.equal(request.cache,'no-store');
  assert(request.signal instanceof AbortSignal);
  return new Response(platform === 'linux' ? linuxFeed(version) : windowsFeed(version));
}});

test('major/minor/patch increments reset lower components and compare numerically', () => {
  assert.equal(incrementVersion('0.3.9','patch'),'0.3.10');
  assert.equal(incrementVersion('0.3.9','minor'),'0.4.0');
  assert.equal(incrementVersion('0.3.9','major'),'1.0.0');
  assert.equal(incrementVersion('0.65535.65535','major'),'1.0.0');
  for (const [version, increment] of [['0.3.9','latest'],['0.3.65535','patch'],['65535.0.0','major'],['01.2.3','patch'],['1.2.3\nVERSION=9.9.9','minor']])
    assert.throws(()=>incrementVersion(version,increment));
});

test('each platform selects from its own published metadata and reports both versions', async () => {
  for (const platform of ['windows','linux']) {
    for (const [increment, version] of [['patch','0.3.10'],['minor','0.4.0'],['major','1.0.0']]) {
      const result=await selectVersion(platform, increment, options(platform,'0.3.9'));
      assert.equal(result.current,'0.3.9');
      assert.equal(result.seed,null);
      assert.equal(result.version,version);
    }
  }
});

test('only a missing feed uses the CMake starting point; failures never invent a version', async () => {
  for (const platform of ['windows','linux']) {
    const result=await selectVersion(platform,'patch',{fetchImpl:async()=>new Response(null,{status:404})});
    const source=await readFile(new URL(platform==='linux'?'../linux/CMakeLists.txt':'../CMakeLists.txt',import.meta.url),'utf8');
    const seed=source.match(/^set\(CALLAYERS_(?:RELEASE_)?VERSION "([^"]+)"/m)[1];
    assert.equal(result.current,null);
    assert.equal(result.seed,seed);
    assert.equal(result.version,incrementVersion(seed,'patch'));
    for (const status of [403,429,500,503])
      await assert.rejects(selectVersion(platform,'patch',{fetchImpl:async()=>new Response(null,{status})}), /Cannot read/);
    await assert.rejects(selectVersion(platform,'patch',{fetchImpl:async()=>{throw Error('network unavailable');}}),/network unavailable/);
  }
});

test('malformed published metadata is rejected rather than falling back', async () => {
  for (const body of ['error page',linuxFeed('01.2.3'),linuxFeed('1.2.3')+'X-Callayers-Version: 9.9.9\n',linuxFeed('1.2.65536')])
    await assert.rejects(publishedVersion('linux',{fetchImpl:async()=>new Response(body)}),/Invalid/);
  for (const body of ['{}','not JSON',windowsFeed('01.2.3'),windowsFeed('1.2.3').replace('payload','wrong')])
    await assert.rejects(publishedVersion('windows',{fetchImpl:async()=>new Response(body)}));
  await assert.rejects(publishedVersion('linux',{fetchImpl:async()=>new Response('a'.repeat(65537))}),/too large/);
  await assert.rejects(selectVersion('other','patch'),/windows or linux/);
});

test('publication rejects overtaken candidates and permits exact-version retries for immutable-byte checking', async () => {
  for (const platform of ['windows','linux']) {
    await assert.rejects(checkCandidate(platform,'0.3.9',options(platform,'0.3.10')),/newer release/);
    assert.equal((await checkCandidate(platform,'0.3.10',options(platform,'0.3.10'))).version,'0.3.10');
    assert.equal((await checkCandidate(platform,'0.4.0',options(platform,'0.3.10'))).version,'0.4.0');
  }
});

test('selected version reaches subsequent Actions steps, artifact output, and the run summary', async () => {
  const directory=await mkdtemp(path.join(tmpdir(),'callayers-version-test-'));
  try {
    const env={GITHUB_ENV:path.join(directory,'env'),GITHUB_OUTPUT:path.join(directory,'output'),GITHUB_STEP_SUMMARY:path.join(directory,'summary')};
    const result=await selectVersion('linux','minor',options('linux','0.3.9'));
    await reportVersion(result,{env,log:()=>{}});
    assert.equal(await readFile(env.GITHUB_ENV,'utf8'),'VERSION=0.4.0\n');
    assert.equal(await readFile(env.GITHUB_OUTPUT,'utf8'),'current=0.3.9\nversion=0.4.0\n');
    const summary=await readFile(env.GITHUB_STEP_SUMMARY,'utf8');
    assert(summary.includes('Current published version: **0.3.9**'));
    assert(summary.includes('Selected version: **0.4.0** (minor)'));
  } finally { await rm(directory,{recursive:true,force:true}); }
});

test('CLI exports the selected version without publishing or requiring cloud credentials', async () => {
  const preload=`globalThis.fetch=async()=>new Response(${JSON.stringify(linuxFeed('0.3.9'))});`;
  const env={...process.env,GITHUB_ENV:'',GITHUB_OUTPUT:'',GITHUB_STEP_SUMMARY:''};
  delete env.NODE_TEST_CONTEXT;
  const result=spawnSync(process.execPath,['--import',`data:text/javascript,${encodeURIComponent(preload)}`,
    'scripts/release-version.mjs','select','linux','patch'],{encoding:'utf8',env});
  assert.ifError(result.error);
  assert.equal(result.status,0,result.stderr);
  assert.match(result.stdout,/current 0.3.9; selected 0.3.10 \(patch\)/);
});
