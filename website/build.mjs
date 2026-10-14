import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
const source=new URL('./public/',import.meta.url),output=new URL('./dist/',import.meta.url);
await mkdir(output,{recursive:true});
await writeFile(new URL('download-config.json',output),await readFile(new URL('../release/download-config.json',import.meta.url)));
// Reuse the app's existing brand asset; keep one source for both app and website.
await writeFile(new URL('callayers-mark.svg',output),await readFile(new URL('../spec-v2/assets/callayers-mark.svg',import.meta.url)));
for(const name of ['index.html','styles.css','download.js','privacy.html','terms.html','_headers']){
 const content=await readFile(new URL(name,source),'utf8');
 if(!content.trim())throw Error(`Empty website file: ${name}`);
 if(name.endsWith('.html')&&(!content.includes('<title>')||!content.includes('lang="en"')))throw Error(`Missing page metadata: ${name}`);
 await writeFile(new URL(name,output),content);
}
await writeFile(new URL('callayers-zoom-windows.png',output),await readFile(new URL('callayers-zoom-windows.png',source)));
console.log(`Website ready: ${fileURLToPath(output)}`);
