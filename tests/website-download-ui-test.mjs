import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';

const source=new URL('../website/public/',import.meta.url);
const html=await readFile(new URL('index.html',source),'utf8');
const scripts=await Promise.all(['download-ui.js','download.js','linux-download.js'].map(name=>readFile(new URL(name,source),'utf8')));
const base='https://downloads.callayers.com';
const booking='https://calendar.app.google/CVnzAvt71HkYhqK38';
const standard=`curl -fsSL ${base}/linux/install.sh | sudo bash`;
const experimental=standard+' -s -- --allow-experimental';

async function page({platform='Linux x86_64',agent='',hints,blocked=false,published=true,analytics=true,analyticsThrows=false,native=false}={}) {
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
 const links=[...html.matchAll(/<a\b([^>]*\bdata-booking-location="([^"]+)"[^>]*)>([\s\S]*?)<\/a>/g)].map(match=>{
  const link=new Element();link.href=/\bhref="([^"]+)"/.exec(match[1])[1];
  link.dataset.bookingLocation=match[2];link.label=new Element();link.label.textContent=match[3].replace(/<[^>]*>/g,'').trim();
  return link;
 });
 const context={AbortSignal,Uint8Array,TextDecoder,atob,document:{getElementById:id=>elements[id]??null,querySelectorAll:selector=>selector==='[data-booking-location]'?links:[]},
  navigator:{platform,userAgent:agent,userAgentData:hints,clipboard:{async writeText(text){if(blocked)throw Error('clipboard denied');writes.push(text);}}},
  window:analytics?{callayersTrack:(event,properties)=>{if(analyticsThrows)throw Error('blocked');events.push({event,properties});}}:{},
  fetch:async url=>{
   if(url==='/download-config.json')return Response.json({bucket:'callayers-downloads',baseUrl:base});
   if(!published)return new Response(null,{status:404});
   if(url.endsWith('/tester/latest.json'))return Response.json({payload:Buffer.from(JSON.stringify({schema:1,channel:'tester',version:'0.3.9',url:`${base}/releases/0.3.9/CallayersSetup-0.3.9.exe`})).toString('base64')});
   return new Response('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.4\nX-Callayers-Ubuntu: 22.04 24.04 26.04\nDescription: Callayers for Ubuntu LTS x64\n'+(native?'X-Callayers-Native: fedora arch\nX-Callayers-Fedora: 43 44\n':''));
  }};
 for(const script of scripts)await runInNewContext(script,context);
 const choose=value=>{elements['linux-distro'].value=value;elements['linux-distro'].listeners.change();};
 return {elements,links,events,writes,context,choose};
}

test('booking stays primary after Windows release discovery and platform changes',async()=>{
 const p=await page({platform:'Win32'});
 assert.equal(p.elements['windows-download'].hidden,false);
 assert.equal(p.elements['linux-panel'].hidden,true);
 assert.equal(p.elements['installer-link'].href,`${base}/releases/0.3.9/CallayersSetup-0.3.9.exe`);
 assert.equal(p.elements['installer-link'].dataset.version,'0.3.9');
 assert.deepEqual(p.links.map(link=>link.dataset.bookingLocation),['navigation','hero','setup']);
 for(const link of p.links){assert.equal(link.href,booking);assert.match(link.label.textContent,/Book a setup call/);}
 p.elements['choose-linux'].listeners.click();
 for(const link of p.links)assert.equal(link.href,booking);
});

test('Linux self-service defaults to Ubuntu with a working command',async()=>{
 const p=await page();
 assert.equal(p.elements['windows-download'].hidden,true);
 assert.equal(p.elements['linux-panel'].hidden,false);
 assert.equal(p.elements['linux-distro'].value,'ubuntu');
 assert.equal(p.elements['linux-command-panel'].hidden,false);
 assert.equal(p.elements['linux-command'].textContent,standard);
 assert.equal(p.links[0].href,booking);
 assert.equal(p.elements['linux-command'].textContent,standard);
 assert.equal(p.elements['linux-command-panel'].hidden,false);
 assert.match(p.elements['linux-distro-note'].textContent,/22.04 \/ 24.04 \/ 26.04/);
 for(const distribution of ['debian','mint','pop']){
  p.choose(distribution);
  assert.equal(p.elements['linux-command'].textContent,experimental);
  assert.match(p.elements['linux-distro-note'].textContent,/Experimental/);
  assert.equal(p.elements['linux-support-badge'].textContent,'Experimental');
  p.context.window.callayersDownloads.refresh();
  p.elements['choose-windows'].listeners.click();
  p.elements['choose-linux'].listeners.click();
  assert.equal(p.elements['linux-distro'].value,distribution);
  assert.equal(p.elements['linux-command'].textContent,experimental);
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
  assert.equal(p.links[0].href,booking);
  p.elements['choose-windows'].listeners.click();
  assert.equal(p.elements['windows-download'].hidden,false);
  p.elements['choose-linux'].listeners.click();
  assert.equal(p.elements['linux-distro'].value,'ubuntu');
  assert.equal(p.elements['linux-command-panel'].hidden,false);
 }
});

test('unpublished feeds never expose a direct download or copy command',async()=>{
 const p=await page({platform:'Win32',published:false});
 assert.equal(p.links[0].href,booking);
 assert.equal(p.elements['installer-link'].hidden,true);
 p.elements['choose-linux'].listeners.click();
 assert.equal(p.elements['linux-distro'].value,'ubuntu');
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

test('Fedora and Arch commands require published native repositories, and Fedora enables RPM Fusion only on explicit selection',async()=>{
 const older=await page();
 for(const distro of ['fedora','arch']) {
  older.choose(distro);
  assert.equal(older.elements['linux-command-panel'].hidden,true);
  assert.equal(older.elements['linux-command'].textContent,'');
 }
 const p=await page({native:true});
 p.choose('fedora');
 assert.equal(p.elements['linux-command'].textContent,experimental);
 assert.equal(p.elements['fedora-repository'].hidden,false);
 assert.match(p.elements['linux-distro-note'].textContent,/43 or 44/);
 p.elements['enable-rpmfusion'].checked=true;
 p.elements['enable-rpmfusion'].listeners.change();
 assert.equal(p.elements['linux-command'].textContent,experimental+' --enable-rpmfusion');
 p.choose('arch');
 assert.equal(p.elements['linux-command'].textContent,experimental);
 assert.equal(p.elements['fedora-repository'].hidden,true);
 assert.match(p.elements['linux-distro-note'].textContent,/full system upgrade/);
 p.choose('ubuntu');
 assert.equal(p.elements['linux-command'].textContent,standard);
});

test('Mac browsers select Mac without guessing Intel versus Apple silicon',async()=>{
 const p=await page({platform:'MacIntel'});
 assert.equal(p.elements['macos-panel'].hidden,false);
 assert.equal(p.elements['choose-macos'].attributes['aria-pressed'],'true');
 assert.match(p.links[0].label.textContent,/Book a setup call/);
 assert.equal(p.links[0].href,booking);
 p.elements['choose-linux'].listeners.click();
 assert.equal(p.elements['macos-panel'].hidden,true);
 p.elements['choose-macos'].listeners.click();
 assert.equal(p.elements['linux-panel'].hidden,true);
});

test('self-service starts collapsed and the Windows signing note comes before its download',()=>{
 const disclosure=/<details\b[^>]*\bid="self-service-downloads"[^>]*>/.exec(html);
 assert(disclosure);
 assert.doesNotMatch(disclosure[0],/\bopen(?:\s|=|>)/);
 const summary=/<summary>([\s\S]*?)<\/summary>/.exec(html.slice(disclosure.index))[1];
 assert.equal(summary.replace(/<[^>]*>/g,' ').replace(/\s+/g,' ').trim(),'Prefer to install it yourself? View downloads and setup instructions.');
 const warning=html.indexOf('id="windows-install-note"'),download=html.indexOf('id="installer-link"');
 assert(warning>0&&warning<download);
 assert.match(html,/<a id="installer-link"[^>]*aria-describedby="windows-install-note"/);
 assert.doesNotMatch(html,/data-download-cta|data-download-label/);
});
