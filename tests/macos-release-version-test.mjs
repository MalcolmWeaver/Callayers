import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {publishedMacRelease, selectMacRelease, reportMacRelease} from '../scripts/macos-release-version.mjs';
import {RELEASE_BASE_URL} from '../scripts/release-storage.mjs';

const metadata = (arch, version, build) => ({schema:1, platform:'macos', arch, notarized:true, version, build,
  minimumSystemVersion:'12.3', sha256:'a'.repeat(64),
  url:`${RELEASE_BASE_URL}/macos/releases/${version}/${arch}/Callayers-${version}-${build}-macos-${arch}.zip`});
const options = (intel, arm) => ({sourceImpl:async()=>({version:'0.4.0',builds:['10','8']}), fetchImpl:async (url, request) => {
  assert.equal(request.redirect, 'error'); assert.equal(request.cache, 'no-store');
  assert(request.signal instanceof AbortSignal);
  const arch = /\/macos\/(x86_64|arm64)\/latest.json$/.exec(url)?.[1];
  assert(arch);
  const value = arch === 'x86_64' ? intel : arm;
  return value ? Response.json(metadata(arch, ...value)) : new Response(null, {status:404});
}});

test('uses the newest architecture version and a build above both feeds and source', async () => {
  for (const [increment, expected] of [['patch','0.4.10'],['minor','0.5.0'],['major','1.0.0']]) {
    const result = await selectMacRelease(increment, options(['0.4.9','90'], ['0.4.8','120']));
    assert.equal(result.current,'0.4.9'); assert.equal(result.version,expected); assert.equal(result.build,'121');
  }
});

test('an unpublished architecture does not reset the other architecture release', async () => {
  for (const pair of [[['0.5.0','200'],null], [null,['0.5.0','200']]]) {
    const result = await selectMacRelease('patch', options(...pair));
    assert.equal(result.version,'0.5.1'); assert.equal(result.build,'201');
  }
});

test('advances past a newer development extension and rejects build-number overflow', async () => {
  const config = options(['0.4.0','10'], ['0.4.0','10']);
  config.sourceImpl = async()=>({version:'0.4.0',builds:['25','40']});
  assert.equal((await selectMacRelease('patch',config)).build,'41');
  await assert.rejects(selectMacRelease('patch',options(['0.4.0','999999999999999'],null)),/limit exceeded/);
});

test('first publication advances the frozen source candidate without modifying its metadata', async () => {
  const file = new URL('../macos/Config/AppInfo.plist', import.meta.url);
  const before = await readFile(file,'utf8');
  const sourceVersion = before.match(/<key>CFBundleShortVersionString<\/key>\s*<string>([^<]+)<\/string>/)[1];
  const sourceBuild = Number(before.match(/<key>CFBundleVersion<\/key>\s*<string>([^<]+)<\/string>/)[1]);
  const config = options(null,null); delete config.sourceImpl;
  const result = await selectMacRelease('patch', config);
  assert.equal(result.current,null); assert.equal(result.seed,sourceVersion);
  assert.notEqual(result.version,sourceVersion); assert(BigInt(result.build) > BigInt(sourceBuild));
  assert.equal(await readFile(file,'utf8'),before);
});

test('HTTP, network and malformed metadata failures never choose a fallback release', async () => {
  for (const status of [403,429,500,503])
    await assert.rejects(selectMacRelease('patch',{fetchImpl:async()=>new Response(null,{status})}),/Cannot read/);
  await assert.rejects(selectMacRelease('patch',{fetchImpl:async()=>{throw Error('offline');}}),/offline/);
  const valid = metadata('arm64','0.4.0','10');
  for (const value of [{}, {...valid,arch:'x86_64'}, {...valid,notarized:false}, {...valid,build:'10\nINJECT=1'},
    {...valid,version:'01.4.0'}, {...valid,url:'https://example.com/app.zip'}])
    await assert.rejects(publishedMacRelease('arm64',{fetchImpl:async()=>Response.json(value)}));
  await assert.rejects(publishedMacRelease('arm64',{fetchImpl:async()=>new Response('a'.repeat(8193))}),/too large/);
  await assert.rejects(selectMacRelease('invalid',options(null,null)),/Choose/);
});

test('reports one consistent version and build for both architecture jobs', async () => {
  const directory = await mkdtemp(path.join(tmpdir(),'callayers-mac-version-'));
  try {
    const env = {GITHUB_OUTPUT:path.join(directory,'outputs'),GITHUB_STEP_SUMMARY:path.join(directory,'summary')};
    const result = await selectMacRelease('patch',options(['0.4.0','10'],['0.4.0','10']));
    await reportMacRelease(result,{env,log:()=>{}});
    assert.equal(await readFile(env.GITHUB_OUTPUT,'utf8'),'current=0.4.0\nversion=0.4.1\nbuild=11\n');
    assert.match(await readFile(env.GITHUB_STEP_SUMMARY,'utf8'),/Check-only runs publish nothing/);
  } finally { await rm(directory,{recursive:true,force:true}); }
});
