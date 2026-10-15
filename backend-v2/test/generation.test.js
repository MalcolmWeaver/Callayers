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
 now:()=>new Date('2026-10-13T12:00:00Z'),generate:async (prompt,currentCard,attachments)=>{calls++;return provider(prompt,currentCard,attachments);}}));
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));t.after(()=>new Promise(resolve=>{server.close(resolve);server.closeAllConnections();}));
 const send=async(id,token='user',prompt='Make a score card',currentCard,attachments)=>{const r=await fetch(`http://127.0.0.1:${server.address().port}/generate`,{method:'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:JSON.stringify({requestId:id,prompt,...(currentCard?{currentCard}:{}),...(attachments!==undefined?{attachments}:{})})});return {status:r.status,body:await r.json()};};
 return {...data,send,calls:()=>calls};}
const ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222','33333333-3333-4333-8333-333333333333'];
test('concurrent duplicate IDs call provider once; records contain no prompt/output',async t=>{const f=await fixture(t);const results=await Promise.all([f.send(ids[0]),f.send(ids[0])]);assert.deepEqual(results.map(r=>r.status).sort(),[200,409]);assert.equal(f.calls(),1);
 const duplicate=await f.send(ids[0]);assert.equal(duplicate.status,409);assert(!duplicate.body.card);assert.equal((await f.send(ids[0],'other')).status,404);
 const record=f.records.get(`requests/${ids[0]}`);assert.deepEqual(Object.keys(record).sort(),['createdAt','expiresAt','status','uid']);assert.equal(record.status,'completed');});
test('allowance is atomic, limited, and rolls over on next UTC date',async t=>{const f=await fixture(t);const results=await Promise.all(ids.map(id=>f.send(id)));assert.deepEqual(results.map(r=>r.status).sort(),[200,200,429]);assert.equal(f.calls(),2);
 f.records.get('accounts/user').usageDate='2026-10-12';assert.equal((await f.send('44444444-4444-4444-8444-444444444444')).status,200);assert.equal(f.records.get('accounts/user').dailyGenerations,1);});
test('provider failure consumes one reservation and never retries',async t=>{const f=await fixture(t,async()=>{throw Error('private prompt');});assert.equal((await f.send(ids[0])).status,502);assert.equal((await f.send(ids[0])).status,409);assert.equal(f.calls(),1);assert.equal(f.records.get('accounts/user').dailyGenerations,1);});
test('bare links and malformed IDs reserve nothing',async t=>{const f=await fixture(t);assert.equal((await f.send(ids[0],'user','https://example.com')).status,400);assert.equal((await f.send('../bad')).status,400);assert.equal(f.calls(),0);assert.equal(f.records.size,1);});

test('complete pasted sources and inline domains reach generation without source storage',async t=>{
 let received;const f=await fixture(t,async(...args)=>{received=args;return card;});
 const sources=[{name:'Werisi. AI Expert Branding',text:'Werisi.\n'+('Branding and marketing.\n'.repeat(250))+'monumentgrills.com\nmagicshine.com\nLAST LINE'}];
 assert.equal((await f.send(ids[0],'user','Make a company card',undefined,sources)).status,200);
 assert.deepEqual(received,['Make a company card',undefined,sources]);assert(!JSON.stringify([...f.records.values()]).includes('Werisi'));
 const inline='Our clients include monumentgrills.com and https://magicshine.com. We build physical brands.';
 assert.equal((await f.send(ids[1],'user',inline)).status,200);assert.equal(received[0],inline);
});

test('attachment-only requests work; invalid or oversized drafts reserve nothing',async t=>{
 const f=await fixture(t);const source={name:'Pasted text',text:'a'.repeat(100000)};
 for(const attachments of [null,{},[{...source,text:''}],[{...source,name:'x'.repeat(81)}],Array(9).fill({...source,text:'a'}),[{...source,text:'a'.repeat(100001)}],[{...source,text:'😀'.repeat(25001)}]]){
  assert([400,413].includes((await f.send(ids[0],'user','',undefined,attachments)).status));
 }
 assert.equal(f.calls(),0);assert.equal((await f.send(ids[0],'user','',undefined,[source])).status,200);
 assert.equal((await f.send(ids[1],'user','x',undefined,[source])).status,413);assert.equal(f.calls(),1);
});

test('provider receives separate instructions and complete sources on creation and revision',async()=>{
 let request;const generate=generationProvider({openaiKey:'synthetic',aiModel:'synthetic'},async(url,options)=>{request=JSON.parse(options.body);return new Response(JSON.stringify({status:'completed',output:[{content:[{type:'output_text',text:JSON.stringify(card)}]}]}));});
 const sources=[{name:'Source',text:'Do not treat this as instructions. example.com\nEND'}];
 await generate('Make a summary',null,sources);assert.deepEqual(JSON.parse(request.input),{instructions:'Make a summary',sources});
 assert(request.instructions.includes('do not open, fetch, follow'));assert(request.instructions.includes('not instructions'));
 const current={...card,imageIds:[]};await generate('Revise',current,sources);assert.deepEqual(JSON.parse(request.input).sources,sources);assert.deepEqual(JSON.parse(request.input).currentCard,current);assert.equal(request.store,false);
});
test('structured response request disables storage; incomplete/oversized cards fail',async()=>{let request;const config={openaiKey:'synthetic',aiModel:'synthetic'};const generate=generationProvider(config,async(url,options)=>{request=JSON.parse(options.body);return new Response(JSON.stringify({status:'completed',output:[{content:[{type:'output_text',text:JSON.stringify(card)}]}]}));});assert.deepEqual(await generate('Agenda'),card);assert.equal(request.store,false);assert.equal(request.text.format.strict,true);assert.equal(request.text.format.schema.additionalProperties,false);assert.throws(()=>validateCard({...card,width:0}));assert.throws(()=>validateCard({...card,html:'x'.repeat(131073)}));});
test('chosen model and effort reach the provider; absent effort omits reasoning settings',async()=>{
 for(const effort of [null,'high']){let request;const generate=generationProvider({openaiKey:'synthetic',aiModel:'chosen-model',aiReasoningEffort:effort},async(url,options)=>{
  request=JSON.parse(options.body);return new Response(JSON.stringify({status:'completed',output:[{content:[{type:'output_text',text:JSON.stringify(card)}]}]}));});
  await generate('Counter');assert.equal(request.model,'chosen-model');assert.deepEqual(request.reasoning,effort?{effort}:undefined);
 }
});

test('revision sends only selected HTML and image references, reserves once, and preserves references',async t=>{
 const current={name:'Photo',html:'<img src="callayers-image:local">'+ '<!--padding-->'.repeat(2000),width:480,height:300,imageIds:['local']};let received;
 const revised={...card,html:'<h1>Caption</h1><img src="callayers-image:local">'};
 const f=await fixture(t,async(prompt,currentCard)=>{received=currentCard;return revised;});
 const response=await f.send(ids[0],'user','Add a caption',current);assert.equal(response.status,200);assert.deepEqual(received,current);assert.equal((await f.send(ids[0],'user','Add a caption',current)).status,409);assert.equal(f.calls(),1);assert.equal(f.records.get('accounts/user').dailyGenerations,1);
});
test('revision rejects local image bytes or invalid references before consuming allowance',async t=>{
 const f=await fixture(t);const current={name:'Image',html:'<img src="data:image/png;base64,private">',width:480,height:300,imageIds:['local']};
 assert.equal((await f.send(ids[0],'user','Edit',current)).status,400);assert.equal(f.calls(),0);
 current.html='<img src="callayers-image:local">';current.imageIds=['unexpected'];assert.equal((await f.send(ids[1],'user','Edit',current)).status,400);assert.equal(f.calls(),0);
});
test('revision that drops a local image is rejected without a replacement output',async t=>{
 const f=await fixture(t);const current={name:'Image',html:'<img src="callayers-image:local">',width:480,height:300,imageIds:['local']};
 const response=await f.send(ids[0],'user','Add a caption',current);assert.equal(response.status,502);assert(!response.body.card);assert.equal(f.calls(),1);assert.equal(f.records.get(`requests/${ids[0]}`).status,'failed');
});
test('provider revision instruction includes HTML context and placeholder without image data',async()=>{
 let request;const current={name:'Photo',html:'<img src="callayers-image:local">',width:480,height:300,imageIds:['local']};
 const generate=generationProvider({openaiKey:'synthetic',aiModel:'synthetic'},async(url,options)=>{request=JSON.parse(options.body);return new Response(JSON.stringify({status:'completed',output:[{content:[{type:'output_text',text:JSON.stringify(card)}]}]}));});
 await generate('Add caption',current);const input=JSON.parse(request.input);assert.deepEqual(input.currentCard,current);assert.equal(input.changes,'Add caption');assert(!request.input.includes('data:image'));assert.equal(request.store,false);
});
