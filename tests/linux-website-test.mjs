import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {runInNewContext} from 'node:vm';
const script = await readFile(new URL('../website/public/linux-download.js', import.meta.url), 'utf8');
async function show(body, status = 200) {
  const download = {hidden: true}, label = {textContent: 'Not published'};
  await runInNewContext(script, {AbortSignal, document: {getElementById: id => id === 'linux-download' ? download : label},
    fetch: async url => {
      assert.equal(url, 'https://downloads.callayers.com/linux/apt/dists/stable/InRelease');
      return new Response(body, {status});
    }});
  return {download, label};
}
test('Ubuntu installer appears only after Linux repository publication', async () => {
  assert.equal((await show('', 404)).download.hidden, true);
  assert.equal((await show('error page')).download.hidden, true);
  const page = await show('-----BEGIN PGP SIGNED MESSAGE-----\nX-Callayers-Version: 0.3.2\nDescription: Callayers for Ubuntu 26.04 x64\n');
  assert.equal(page.download.hidden, false);
  assert.equal(page.label.textContent, 'Linux beta 0.3.2 · Ubuntu 26.04 x64');
});
