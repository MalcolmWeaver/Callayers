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
  assert.equal(page.label.textContent, 'Linux: 0.3.2 · Beta · x64');
  assert.equal(page.download.dataset.ubuntu,'26.04');
  assert.equal(page.download.dataset.experimental,'false');
  const lts = await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.3\nX-Callayers-Ubuntu: 22.04 24.04 26.04\nDescription: Callayers for Ubuntu LTS x64\n');
  assert.equal(lts.download.hidden, false);
  assert.equal(lts.label.textContent, 'Linux: 0.3.3 · Beta · x64');
  assert.equal(lts.download.dataset.ubuntu,'22.04 / 24.04 / 26.04');
  assert.equal(lts.download.dataset.experimental,'true');
  assert.equal(lts.download.dataset.version, '0.3.3');
  assert.equal((await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.3\nDescription: Callayers for Ubuntu LTS x64\n')).download.hidden, true);
});
