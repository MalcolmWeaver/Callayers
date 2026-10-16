import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';

const source=new URL('../website/public/',import.meta.url);
const html=await readFile(new URL('index.html',source),'utf8');
const scripts=await Promise.all(['download-ui.js','download.js','linux-download.js'].map(name=>readFile(new URL(name,source),'utf8')));
const base='https://downloads.callayers.com';
const standard=`curl -fsSL ${base}/linux/install.sh | sudo bash`;
const experimental=standard+' -s -- --allow-experimental';

async function page({platform='Linux x86_64',agent='',hints,blocked=false,published=true,analytics=true,analyticsThrows=false}={}) {
 const events=[],writes=[],elements={};
 class Element {
  constructor(hidden=false){this.hidden=hidden;this.dataset={};this.textContent='';this.value='';this.listeners={};this.attributes={};}
  addEventListener(name,callback){this.listeners[name]=callback;}
  setAttribute(name,value){this.attributes[name]=value;}
  focus(){this.focused=true;}
  scrollIntoView(){this.scrolled=true;}
  querySelector(){return this.label;}
 }
 // Build fixtures from the actual page IDs and initial hidden attributes.
 for(const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g))elements[match[1]]=new Element(/\bhidden\b/.test(match[0]));
 const links=[new Element(),new Element()];
 for(const link of links)link.label=new Element();
 const context={AbortSignal,Uint8Array,TextDecoder,atob,document:{getElementById:id=>elements[id]??null,querySelectorAll:()=>links},
  navigator:{platform,userAgent:agent,userAgentData:hints,clipboard:{async writeText(text){if(blocked)throw Error('clipboard denied');writes.push(text);}}},
  window:analytics?{callayersTrack:(event,properties)=>{if(analyticsThrows)throw Error('blocked');events.push({event,properties});}}:{},
  fetch:async url=>{
   if(url==='/download-config.json')return Response.json({bucket:'callayers-downloads',baseUrl:base});
   if(!published)return new Response(null,{status:404});
   if(url.endsWith('/tester/latest.json'))return Response.json({payload:Buffer.from(JSON.stringify({schema:1,channel:'tester',version:'0.3.9',url:`${base}/releases/0.3.9/CallayersSetup-0.3.9.exe`})).toString('base64')});
   return new Response('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.4\nX-Callayers-Ubuntu: 22.04 24.04 26.04\nDescription: Callayers for Ubuntu LTS x64\n');
  }};
 for(const script of scripts)await runInNewContext(script,context);
 const choose=value=>{elements['linux-distro'].value=value;elements['linux-distro'].listeners.change();};
 return {elements,links,events,writes,context,choose};
}

test('Windows browser gets direct, versioned downloads at both top links',async()=>{
 const p=await page({platform:'Win32'});
 assert.equal(p.elements['windows-download'].hidden,false);
 assert.equal(p.elements['linux-panel'].hidden,true);
 for(const link of p.links){
  assert.equal(link.href,`${base}/releases/0.3.9/CallayersSetup-0.3.9.exe`);
  assert.equal(link.dataset.downloadPlatform,'windows');
  assert.equal(link.dataset.version,'0.3.9');
  assert.equal(link.label.textContent,'Download for Windows');
 }
 p.elements['choose-linux'].listeners.click();
 assert.equal(p.links[0].href,'#download');
 assert.equal(p.links[0].dataset.downloadPlatform,'');
 assert.equal(p.links[0].dataset.version,undefined);
});

test('Linux browser gets a distribution picker and top links focus it',async()=>{
 const p=await page();
 assert.equal(p.elements['windows-download'].hidden,true);
 assert.equal(p.elements['linux-panel'].hidden,false);
 assert.equal(p.elements['linux-command-panel'].hidden,true);
 assert.equal(p.links[0].href,'#download');
 assert.equal(p.links[0].label.textContent,'Install for Linux');
 let prevented=false;
 p.links[0].listeners.click({preventDefault(){prevented=true;}});
 assert.equal(prevented,true);
 assert.equal(p.elements.download.scrolled,true);
 assert.equal(p.elements['linux-distro'].focused,true);
 p.choose('ubuntu');
 assert.equal(p.elements['linux-command'].textContent,standard);
 assert.equal(p.elements['linux-command-panel'].hidden,false);
 assert.match(p.elements['linux-distro-note'].textContent,/22.04 \/ 24.04 \/ 26.04/);
 for(const distribution of ['debian','mint','pop']){
  p.choose(distribution);
  assert.equal(p.elements['linux-command'].textContent,experimental);
  assert.match(p.elements['linux-distro-note'].textContent,/Experimental/);
  assert.equal(p.elements['linux-support-badge'].textContent,'Experimental');
 }
 p.choose('other');
 assert.equal(p.elements['linux-command'].textContent,'');
 assert.equal(p.elements['linux-command-panel'].hidden,true);
 assert.match(p.elements['linux-distro-note'].textContent,/not currently supported/);
 await p.elements['copy-linux'].listeners.click();
 assert.equal(p.writes.length,0);
});

test('client hints work, while mobile, Mac, and unknown browsers can choose a platform explicitly',async()=>{
 const hinted=await page({platform:'',hints:{platform:'Windows',mobile:false}});
 assert.equal(hinted.elements['windows-download'].hidden,false);
 for(const config of [{platform:'MacIntel'},{platform:''},{platform:'Linux aarch64',agent:'Android'}, {platform:'Linux',agent:'CrOS'}]) {
  const p=await page(config);
  assert.equal(p.elements['windows-download'].hidden,true);
  assert.equal(p.elements['linux-panel'].hidden,true);
  assert.equal(p.links[0].href,'#download');
  p.elements['choose-windows'].listeners.click();
  assert.equal(p.elements['windows-download'].hidden,false);
 }
});

test('unpublished feeds never expose a direct download or copy command',async()=>{
 const p=await page({platform:'Win32',published:false});
 assert.equal(p.links[0].href,'#download');
 p.elements['choose-linux'].listeners.click();
 p.choose('ubuntu');
 assert.equal(p.elements['linux-command-panel'].hidden,true);
 await p.elements['copy-linux'].listeners.click();
 assert.equal(p.writes.length,0);
});

test('copy writes the selected command and records its version/distribution only after success',async()=>{
 const p=await page();p.choose('debian');
 await p.elements['copy-linux'].listeners.click();
 assert.deepEqual(p.writes,[experimental]);
 assert.deepEqual(JSON.parse(JSON.stringify(p.events)),[{event:'linux_install_command_copied',properties:{platform:'linux',app_version:'0.3.4',distribution:'debian',button_location:'download'}}]);
 const blocked=await page({blocked:true});blocked.choose('ubuntu');
 await blocked.elements['copy-linux'].listeners.click();
 assert.equal(blocked.events.length,0);
 assert.match(blocked.elements['copy-linux-status'].textContent,/manually/);
 for(const config of [{analytics:false},{analyticsThrows:true}]){
  const unavailable=await page(config);unavailable.choose('ubuntu');
  await unavailable.elements['copy-linux'].listeners.click();
  assert.deepEqual(unavailable.writes,[standard]);
  assert.match(unavailable.elements['copy-linux-status'].textContent,/Copied/);
 }
});
