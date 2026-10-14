// One-time provisioning. Credentials are read only from the environment.
const token=process.env.CLOUDFLARE_API_TOKEN,account=process.env.CLOUDFLARE_ACCOUNT_ID;
if(!token||!/^[a-f0-9]{32}$/.test(account??''))throw Error('Set CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID first.');
async function api(route,method='GET',body,allow404=false){
 const response=await fetch(`https://api.cloudflare.com/client/v4${route}`,{method,headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body)});
 if(allow404&&response.status===404)return null;
 const data=await response.json();
 if(!response.ok||!data.success)throw Error(`Cloudflare ${method} ${route} failed (${response.status}); check permissions and resource state.`);
 return data.result;
}
const zones=await api(`/zones?name=callayers.com&account.id=${account}`);
if(zones.length!==1)throw Error('Cannot find callayers.com in the selected Cloudflare account.');
const zone=zones[0].id;
const projectRoute=`/accounts/${account}/pages/projects/callayers`;
let project=await api(projectRoute,'GET',undefined,true);
if(!project)project=await api(`/accounts/${account}/pages/projects`,'POST',{name:'callayers',production_branch:'main'});
if(project.source)throw Error('Existing Pages project uses Git integration. Review it before switching to this workflow.');
const records=await api(`/zones/${zone}/dns_records?name=callayers.com`);
const addressRecords=records.filter(r=>['A','AAAA','CNAME'].includes(r.type));
if(addressRecords.some(r=>r.type!=='CNAME'||r.content!=='callayers.pages.dev'))throw Error('Existing website DNS conflicts; no DNS records were replaced.');
const domains=await api(`${projectRoute}/domains`);
if(!domains.some(d=>d.name==='callayers.com'))await api(`${projectRoute}/domains`,'POST',{name:'callayers.com'});
if(!addressRecords.length)await api(`/zones/${zone}/dns_records`,'POST',{type:'CNAME',name:'callayers.com',content:'callayers.pages.dev',proxied:true,ttl:1});
const bucketRoute=`/accounts/${account}/r2/buckets/callayers-downloads`;
if(!await api(bucketRoute,'GET',undefined,true))await api(`/accounts/${account}/r2/buckets`,'POST',{name:'callayers-downloads'});
const bucketDomains=await api(`${bucketRoute}/domains/custom`);
const domainsList=Array.isArray(bucketDomains)?bucketDomains:bucketDomains.domains;
if(!domainsList.some(d=>d.domain==='downloads.callayers.com'))await api(`${bucketRoute}/domains/custom`,'POST',{domain:'downloads.callayers.com',enabled:true,zoneId:zone});
await api(`${bucketRoute}/cors`,'PUT',{rules:[{allowed:{origins:['https://callayers.com'],methods:['GET','HEAD'],headers:['*']},maxAgeSeconds:3600}]});
console.log('Cloudflare resources prepared. Configure GitHub credentials, deploy the website, then explicitly publish the tester installer. Domain certificates may need time to become active.');
