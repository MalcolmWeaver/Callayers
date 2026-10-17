import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
const script=await readFile(new URL('../website/public/macos-download.js',import.meta.url),'utf8');
const html=await readFile(new URL('../website/public/index.html',import.meta.url),'utf8');
const release=arch=>({schema:1,platform:'macos',arch,notarized:true,version:'0.4.0',build:'9',minimumSystemVersion:'12.3',sha256:'a'.repeat(64),url:`https://downloads.callayers.com/macos/releases/0.4.0/${arch}/Callayers-0.4.0-9-macos-${arch}.zip`});
async function page(change=value=>value,status=200){
 const elements={};for(const arch of ['arm64','x86_64'])for(const suffix of ['link','status'])elements[`macos-${arch}-${suffix}`]={hidden:true,dataset:{}};
 await runInNewContext(script,{AbortSignal,document:{getElementById:id=>elements[id]},fetch:async url=>{
  const arch=url.includes('/arm64/')?'arm64':'x86_64';return new Response(JSON.stringify(change(release(arch))),{status});
 }});return elements;
}
test('Mac downloads expose only matching architecture versioned release URLs',async()=>{
 const p=await page();for(const arch of ['arm64','x86_64']){
  assert.equal(p[`macos-${arch}-link`].href,release(arch).url);assert.equal(p[`macos-${arch}-link`].hidden,false);
  assert.match(p[`macos-${arch}-status`].textContent,/macOS 12.3 or later/);
 }
});
test('Mac choices disclose Intel beta and untested Apple silicon before download',async()=>{
 assert.match(html,/<a id="macos-x86_64-link"[^>]*>Download for Intel Mac \(beta\)/);
 assert.match(html,/<p id="macos-arm64-note"[^>]*>Experimental: this build has not been tested on Apple silicon hardware\.<\/p>\s*<a id="macos-arm64-link"[^>]*aria-describedby="macos-arm64-note"[^>]*>Download for Apple silicon \(experimental\)/);
 const p=await page();
 assert.match(p['macos-x86_64-status'].textContent,/^Intel beta:/);
 assert.match(p['macos-arm64-status'].textContent,/^Apple silicon experimental:/);
});
test('unpublished, unnotarized, malformed and foreign Mac release data stay unavailable',async()=>{
 for(const [change,status]of [[x=>x,404],[x=>x,503],[x=>({...x,notarized:false}),200],[x=>({...x,url:'https://example.com/app.zip'}),200],[x=>({...x,arch:'other'}),200],[x=>({...x,version:'../bad'}),200],[x=>({...x,sha256:''}),200]]){
  const p=await page(change,status);for(const arch of ['arm64','x86_64'])assert.equal(p[`macos-${arch}-link`].hidden,true);
 }
});
