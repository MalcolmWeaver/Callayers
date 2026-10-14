import test from 'node:test';import assert from 'node:assert/strict';import {createServer} from 'node:http';
import {createHandler} from '../src/server.js';import {accountStore} from '../src/accounts.js';import {generationProvider,validateCard} from '../src/generation.js';
const card={name:'Score',html:'<button>+1</button>',width:400,height:200};
function memoryStore(){const records=new Map();let queue=Promise.resolve();
 const db={collection:collection=>({doc:id=>`${collection}/${id}`}),runTransaction:fn=>{
  const work=queue.then(()=>fn({get:async ref=>({exists:records.has(ref),data:()=>structuredClone(records.get(ref))}),
   create:(ref,value)=>{assert(!records.has(ref));records.set(ref,structuredClone(value));},update:(ref,value)=>{assert(records.has(ref));Object.assign(records.get(ref),value);}}));queue=work.catch(()=>{});return work;}};
 return {records,store:accountStore(db,()=>0,date=>date.toISOString())};}
async function fixture(t,provider=async()=>card){let calls=0;const data=memoryStore();
 const server=createServer(createHandler({config:{projectId:'test',publicConfig:{},dailyLimit:2},accounts:data.store,
 auth:{verifyIdToken:async token=>({uid:token==='other'?'other':'user',aud:'test',iss:'https://securetoken.google.com/test',email_verified:true,firebase:{sign_in_provider:'google.com'}})},
 now:()=>new Date('2026-10-13T12:00:00Z'),generate:async prompt=>{calls++;return provider(prompt);}}));
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));t.after(()=>new Promise(resolve=>{server.close(resolve);server.closeAllConnections();}));
 const send=async(id,token='user',prompt='Make a score card')=>{const r=await fetch(`http://127.0.0.1:${server.address().port}/generate`,{method:'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:JSON.stringify({requestId:id,prompt})});return {status:r.status,body:await r.json()};};
 return {...data,send,calls:()=>calls};}
const ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222','33333333-3333-4333-8333-333333333333'];
test('concurrent duplicate IDs call provider once; records contain no prompt/output',async t=>{const f=await fixture(t);const results=await Promise.all([f.send(ids[0]),f.send(ids[0])]);assert.deepEqual(results.map(r=>r.status).sort(),[200,409]);assert.equal(f.calls(),1);
 const duplicate=await f.send(ids[0]);assert.equal(duplicate.status,409);assert(!duplicate.body.card);assert.equal((await f.send(ids[0],'other')).status,404);
 const record=f.records.get(`requests/${ids[0]}`);assert.deepEqual(Object.keys(record).sort(),['createdAt','expiresAt','status','uid']);assert.equal(record.status,'completed');});
test('allowance is atomic, limited, and rolls over on next UTC date',async t=>{const f=await fixture(t);const results=await Promise.all(ids.map(id=>f.send(id)));assert.deepEqual(results.map(r=>r.status).sort(),[200,200,429]);assert.equal(f.calls(),2);
 f.records.get('accounts/user').usageDate='2026-10-12';assert.equal((await f.send('44444444-4444-4444-8444-444444444444')).status,200);assert.equal(f.records.get('accounts/user').dailyGenerations,1);});
test('provider failure consumes one reservation and never retries',async t=>{const f=await fixture(t,async()=>{throw Error('private prompt');});assert.equal((await f.send(ids[0])).status,502);assert.equal((await f.send(ids[0])).status,409);assert.equal(f.calls(),1);assert.equal(f.records.get('accounts/user').dailyGenerations,1);});
test('website prompts and malformed IDs reserve nothing',async t=>{const f=await fixture(t);assert.equal((await f.send(ids[0],'user','Summarize https://example.com')).status,400);assert.equal((await f.send('../bad')).status,400);assert.equal(f.calls(),0);assert.equal(f.records.size,1);});
test('structured response request disables storage; incomplete/oversized cards fail',async()=>{let request;const config={openaiKey:'synthetic',aiModel:'synthetic'};const generate=generationProvider(config,async(url,options)=>{request=JSON.parse(options.body);return new Response(JSON.stringify({status:'completed',output:[{content:[{type:'output_text',text:JSON.stringify(card)}]}]}));});assert.deepEqual(await generate('Agenda'),card);assert.equal(request.store,false);assert.equal(request.text.format.strict,true);assert.equal(request.text.format.schema.additionalProperties,false);assert.throws(()=>validateCard({...card,width:0}));assert.throws(()=>validateCard({...card,html:'x'.repeat(131073)}));});
