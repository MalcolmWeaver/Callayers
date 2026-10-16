import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
const script = await readFile(new URL('../website/public/linux-download.js', import.meta.url), 'utf8');
async function show(body, status = 200) {
  const download = {hidden: true, dataset: {}}, label = {textContent: 'Not published'};
  await runInNewContext(script, {AbortSignal, document: {getElementById: id => id === 'linux-download' ? download : label},
    fetch: async url => {
      assert.equal(url, 'https://downloads.callayers.com/linux/apt/dists/stable/InRelease');
      return new Response(body, {status});
    }});
  return {download, label};
}
test('Ubuntu installer appears only after Linux repository publication', async () => {
  const unpublished=await show('',404);
  assert.equal(unpublished.download.hidden,true);
  assert.equal(unpublished.label.textContent,'Linux: Not published yet.');
  for(const [body,status] of [['error page',200],['',503]]) {
    const unavailable=await show(body,status);
    assert.equal(unavailable.download.hidden,true);
    assert.equal(unavailable.label.textContent,'Linux: Release status unavailable. Try again shortly.');
  }
  const page = await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.2\nDescription: Callayers for Ubuntu 26.04 x64\n');
  assert.equal(page.download.hidden, false);
  assert.equal(page.label.textContent, 'Linux: 0.3.2 · Beta · Ubuntu 26.04 x64');
  const lts = await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.3\nX-Callayers-Ubuntu: 22.04 24.04 26.04\nDescription: Callayers for Ubuntu LTS x64\n');
  assert.equal(lts.download.hidden, false);
  assert.equal(lts.label.textContent, 'Linux: 0.3.3 · Beta · Ubuntu 22.04 / 24.04 / 26.04 x64');
  assert.equal(lts.download.dataset.version, '0.3.3');
  assert.equal((await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.3\nDescription: Callayers for Ubuntu LTS x64\n')).download.hidden, true);
});

test('Linux copy event follows a successful clipboard write and carries the published version', async () => {
  const downloadScript = await readFile(new URL('../website/public/download.js', import.meta.url), 'utf8');
  async function copy({published=true, blocked=false, analytics=true, analyticsThrows=false}={}) {
    let click;
    const events=[], writes=[];
    const elements={
      'linux-download':{hidden:true,dataset:{}},
      'linux-release-status':{textContent:''},
      'linux-command':{textContent:'curl -fsSL https://downloads.callayers.com/linux/install.sh | sudo bash'},
      'copy-linux-status':{textContent:''},
      'copy-linux':{addEventListener(name,callback){assert.equal(name,'click');click=callback;}},
    };
    const context={AbortSignal,document:{getElementById:id=>elements[id]??null},
      fetch:async()=>new Response(published?'-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.4\nX-Callayers-Ubuntu: 22.04 24.04 26.04\nDescription: Callayers for Ubuntu LTS x64\n':'',{status:published?200:404}),
      navigator:{clipboard:{async writeText(text){if(blocked)throw Error('denied');writes.push(text);}}},
      window:analytics?{callayersTrack:(event,properties)=>{if(analyticsThrows)throw Error('analytics unavailable');events.push({event,properties});}}:{},
    };
    await runInNewContext(script,context);
    await runInNewContext(downloadScript,context);
    await click();
    return {events,writes,status:elements['copy-linux-status'].textContent};
  }
  const copied=await copy();
  assert.equal(copied.writes.length,1);
  assert.deepEqual(JSON.parse(JSON.stringify(copied.events)),[{event:'linux_install_command_copied',properties:{platform:'linux',app_version:'0.3.4',button_location:'download'}}]);
  assert.deepEqual((await copy({blocked:true})).events,[]);
  assert.deepEqual((await copy({published:false})).writes,[]);
  const withoutAnalytics=await copy({analytics:false});
  assert.equal(withoutAnalytics.writes.length,1);
  assert.equal(withoutAnalytics.status,'Copied. Paste it into a terminal.');
  assert.equal((await copy({analyticsThrows:true})).status,'Copied. Paste it into a terminal.');
});
