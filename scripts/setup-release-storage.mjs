import {RELEASE_BUCKET} from './release-storage.mjs';
const token=process.env.CLOUDFLARE_API_TOKEN,account=process.env.CLOUDFLARE_ACCOUNT_ID;
if(!token||!/^[a-f0-9]{32}$/.test(account??''))throw Error('Configure Cloudflare account and API token.');
async function api(route,method='GET',body,allow404=false){
 const response=await fetch(`https://api.cloudflare.com/client/v4${route}`,{method,redirect:'error',signal:AbortSignal.timeout(120000),
  headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body)});
 if(allow404&&response.status===404)return null;
 const data=await response.json();
 if(!response.ok||!data.success)throw Error(`Cloudflare ${method} failed (${response.status}); check permissions and R2 activation.`);
 return data.result;
}
const zones=await api(`/zones?name=callayers.com&account.id=${account}`);
if(zones.length!==1)throw Error('Cannot find callayers.com in the selected account.');
const zone=zones[0].id,bucketRoute=`/accounts/${account}/r2/buckets/${RELEASE_BUCKET}`;
if(!await api(bucketRoute,'GET',undefined,true))await api(`/accounts/${account}/r2/buckets`,'POST',{name:RELEASE_BUCKET});
const existing=await api(`${bucketRoute}/domains/custom`);
const domains=Array.isArray(existing)?existing:existing.domains;
if(!Array.isArray(domains))throw Error('Unexpected R2 domain response.');
const domain=domains.find(item=>item.domain==='downloads.callayers.com');
if(!domain)await api(`${bucketRoute}/domains/custom`,'POST',{domain:'downloads.callayers.com',enabled:true,zoneId:zone});
else if(!domain.enabled)throw Error('The download domain is disabled; enable it before publication.');
await api(`${bucketRoute}/cors`,'PUT',{rules:[{allowed:{origins:['https://callayers.com'],methods:['GET','HEAD'],headers:['*']},maxAgeSeconds:3600}]});
console.log('R2 downloads configured. Wait for downloads.callayers.com to become active, then explicitly publish a tester release.');
