import { readFile } from 'node:fs/promises';
import { createHash, createPublicKey, verify } from 'node:crypto';
import path from 'node:path';
import {createReleaseStore, RETENTION_KEY, prepareRetention, compareVersions, cleanupReleases} from './release-retention.mjs';
const version = process.argv[2];
if (!/^(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/.test(version ?? '') || version.split('.').some(v=>+v>65535)) throw Error('Invalid release version.');
const token=process.env.CLOUDFLARE_API_TOKEN, account=process.env.CLOUDFLARE_ACCOUNT_ID;
if(!token || !/^[a-f0-9]{32}$/.test(account ?? '')) throw Error('Configure Cloudflare account and API token.');
const bucket='callayers-downloads';
const key=`releases/${version}/CallayersSetup-${version}.exe`;
const directory=path.resolve('build/installers',version);
const envelope=await readFile(path.join(directory,'latest.json'));
const record=JSON.parse(envelope);
const payload=JSON.parse(Buffer.from(record.payload,'base64').toString('utf8'));
const installer=await readFile(path.join(directory,`CallayersSetup-${version}.exe`));
if(payload.schema!==1 || payload.channel!=='tester' || payload.version!==version || payload.url!==`https://downloads.callayers.com/${key}` || payload.size!==installer.length || payload.sha256!==createHash('sha256').update(installer).digest('hex')) throw Error('Installer does not match release manifest.');
// Publishing verifies with the same public key that packaging embedded.
const signingKey=JSON.parse(await readFile(path.join(directory,'public-key.json'),'utf8'));
{
  const publicKey=createPublicKey({key:{kty:'RSA',n:Buffer.from(signingKey.Modulus,'base64').toString('base64url'),e:Buffer.from(signingKey.Exponent,'base64').toString('base64url')},format:'jwk'});
  if(!verify('sha256',Buffer.from(record.payload,'base64'),publicKey,Buffer.from(record.signature,'base64'))) throw Error('Manifest signature rejected.');
}
const headers={Authorization:`Bearer ${token}`};
const endpoint=`https://api.cloudflare.com/client/v4/accounts/${account}/r2/buckets/${bucket}/objects/`;
const objectUrl=k=>endpoint+k.split('/').map(encodeURIComponent).join('/');
const previous=await fetch(objectUrl('tester/latest.json'),{headers});
const store=createReleaseStore({token,account});
const retention=await store.readJson(RETENTION_KEY);
let priorVersion=null;
if(previous.ok){
 const prior=JSON.parse(Buffer.from((await previous.json()).payload,'base64').toString('utf8'));
 priorVersion=prior.version;
 if(compareVersions(prior.version,version)>0)throw Error('A newer tester release is already published.');
 if(compareVersions(prior.version,version)===0&&prior.sha256!==payload.sha256)throw Error('Published versions are immutable. Choose a new version.');
}else if(previous.status!==404)throw Error(`Cloudflare feed preflight failed (${previous.status}).`);
const existing=await fetch(objectUrl(key),{headers});
if(existing.ok){
 if(createHash('sha256').update(Buffer.from(await existing.arrayBuffer())).digest('hex')!==payload.sha256)throw Error('This version already exists with different bytes. Publish a new version.');
}else if(existing.status!==404)throw Error(`Cloudflare preflight failed (${existing.status}).`);
// Wrangler's supported upload path sets stored HTTP metadata, including feed cache policy.
const {spawnSync}=await import('node:child_process');
function put(k,file,type,cacheControl){
 const args=['--yes','wrangler@4.77.0','r2','object','put',`${bucket}/${k}`,'--file',file,'--content-type',type,'--cache-control',cacheControl,'--remote'];
 const result=process.platform==='win32'
   ?spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.resolve('scripts/upload-r2.ps1'),'-Object',`${bucket}/${k}`,'-File',file,'-ContentType',type,'-CacheControl',cacheControl],{stdio:'inherit'})
   :spawnSync('npx',args,{stdio:'inherit'});
 if(result.status!==0)throw Error('Cloudflare upload failed.');
}
if(existing.status===404)put(key,path.join(directory,`CallayersSetup-${version}.exe`),'application/vnd.microsoft.portable-executable','public,max-age=31536000,immutable');
const url=`https://downloads.callayers.com/${key}`;
const publicFile=await fetch(url,{cache:'no-store',redirect:'error',signal:AbortSignal.timeout(120000)});
if(!publicFile.ok)throw Error('Public installer is unavailable; update feed was not changed.');
const publicBytes=Buffer.from(await publicFile.arrayBuffer());
if(createHash('sha256').update(publicBytes).digest('hex')!==payload.sha256)throw Error('Public installer checksum mismatch; update feed was not changed.');
// Save protection/deadlines first. If feed publication fails, cleanup refuses mismatched metadata.
const nextRetention=prepareRetention(retention,version,priorVersion,await store.listInstallers());
await store.writeRetention(nextRetention);
put('tester/latest.json',path.join(directory,'latest.json'),'application/json','no-store');
console.log(`Published tester release ${version}: ${url}`);
await cleanupReleases(store);
