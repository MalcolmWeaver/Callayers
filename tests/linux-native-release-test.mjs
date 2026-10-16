// Runs against a locally prepared signed repository; all storage/network calls
// are replaced with an in-memory store. No publication or cloud credentials.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdtemp,cp,rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {publishLinux} from '../scripts/publish-linux.mjs';
import {RELEASE_BASE_URL} from '../scripts/release-storage.mjs';

const repository=process.env.CALLAYERS_TEST_NATIVE_REPOSITORY;
const head='linux/apt/dists/stable/InRelease';
const hash=value=>createHash('sha256').update(value).digest('hex');
function store(initial=new Map(),fail=false){
 const objects=new Map(initial),writes=[];
 return {objects,writes,store:{async readBytes(key){return objects.get(key)??null;},async writeObject(key,bytes){writes.push(key);objects.set(key,bytes);}},
  fetchImpl:async url=>{const value=objects.get(url.slice(RELEASE_BASE_URL.length+1));return new Response(fail?null:value,{status:fail?503:value?200:404});}};
}
test('native publication validates signed indices, uploads packages first, and exposes capabilities last',{skip:!repository},async()=>{
 const state=store();
 await publishLinux({directory:repository,...state});
 const manifest=JSON.parse(await readFile(path.join(repository,'manifest.json')));
 assert.equal(state.writes.at(-1),head);
 for(const [file,index]of [[`linux/rpm/packages/callayers-${manifest.version}-1.x86_64.rpm`,'linux/rpm/repodata/repomd.xml'],
  [`linux/arch/x86_64/callayers-${manifest.version}-1-x86_64.pkg.tar.zst`,'linux/arch/x86_64/callayers.db']]){
  assert(state.writes.indexOf(file)<state.writes.indexOf(index));
  assert(state.writes.indexOf(index)<state.writes.indexOf(head));
 }
 assert.match(state.objects.get(head).toString(),/X-Callayers-Native: fedora arch/);
});
test('native package replacements and failed public downloads stop before capability publication',{skip:!repository},async()=>{
 const manifest=JSON.parse(await readFile(path.join(repository,'manifest.json')));
 const rpm=`linux/rpm/packages/callayers-${manifest.version}-1.x86_64.rpm`;
 const existing=store(new Map([[rpm,Buffer.from('different package')]]));
 await assert.rejects(publishLinux({directory:repository,...existing}),/immutable/);
 assert.deepEqual(existing.writes,[]);
 const failed=store(new Map(),true);
 await assert.rejects(publishLinux({directory:repository,...failed}),/Public repository/);
 assert(!failed.writes.includes(head));
});
test('changing a native index and matching manifest checksum cannot bypass its signature',{skip:!repository},async()=>{
 const directory=await mkdtemp(path.join(tmpdir(),'callayers-native-tamper-'));
 try{
  await cp(repository,directory,{recursive:true,dereference:true});
  const key='linux/rpm/repodata/repomd.xml';
  const file=path.join(directory,key);
  const bytes=Buffer.concat([await readFile(file),Buffer.from('\n<!-- changed -->\n')]);
  await writeFile(file,bytes);
  const manifest=JSON.parse(await readFile(path.join(directory,'manifest.json')));
  Object.assign(manifest.files.find(item=>item.key===key),{sha256:hash(bytes),size:bytes.length});
  await writeFile(path.join(directory,'manifest.json'),JSON.stringify(manifest));
  const state=store();
  await assert.rejects(publishLinux({directory,...state}),/Native repository/);
  assert.deepEqual(state.writes,[]);
 }finally{await rm(directory,{recursive:true,force:true});}
});
