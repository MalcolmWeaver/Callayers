import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const source=new URL('./public/',import.meta.url),output=new URL('./dist/',import.meta.url);
await mkdir(output,{recursive:true});
await writeFile(new URL('download-config.json',output),await readFile(new URL('../release/download-config.json',import.meta.url)));
// Reuse the app's existing brand asset; keep one source for both app and website.
await writeFile(new URL('callayers-mark.svg',output),await readFile(new URL('../spec-v2/assets/callayers-mark.svg',import.meta.url)));
for(const name of ['index.html','styles.css','download-ui.js','download.js','linux-download.js','analytics.js','signed-in.html','privacy.html','terms.html','_headers']){
 let content=await readFile(new URL(name,source),'utf8');
 if(!content.trim())throw Error(`Empty website file: ${name}`);
 if(name.endsWith('.html')&&(!content.includes('<title>')||!content.includes('lang="en"')))throw Error(`Missing page metadata: ${name}`);
 // New page markup must load matching styles and scripts, even after a cached release.
 if(name.endsWith('.html')){
  const assets=[...new Set([...content.matchAll(/(?:href|src)="\/(\w[\w.-]*\.(?:css|js))"/g)].map(match=>match[1]))];
  for(const asset of assets){
   const hash=createHash('sha256').update(await readFile(new URL(asset,source))).digest('hex').slice(0,12);
   content=content.replaceAll(`"/${asset}"`,`"/${asset}?v=${hash}"`);
  }
 }
 await writeFile(new URL(name,output),content);
}
await writeFile(new URL('callayers-zoom-windows.png',output),await readFile(new URL('callayers-zoom-windows.png',source)));
console.log(`Website ready: ${fileURLToPath(output)}`);
