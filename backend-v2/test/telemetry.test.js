import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createHandler } from '../src/server.js';
import { validateEvents, telemetryProvider, telemetryLimiter } from '../src/telemetry.js';
import { loadConfig } from '../src/config.js';
const now=new Date('2026-10-14T02:00:00.000Z');
const sample={id:'12345678-1234-4123-8123-123456789abc',sessionId:'12345678-1234-4123-9123-123456789abc',timestamp:now.toISOString(),appVersion:'0.2.0-telemetry.1',event:'session_started',properties:{}};
const config={projectId:'synthetic',publicConfig:{version:2}};
const identity={uid:'owner',aud:'synthetic',iss:'https://securetoken.google.com/synthetic',email_verified:true,firebase:{sign_in_provider:'google.com'}};
const account={schemaVersion:2,access:'enabled',usageDate:'2026-10-14',dailyGenerations:3};
async function request(t,{body={events:[sample]},authorization='Bearer synthetic.token',contentType='application/json',statusError=false,configured=true,enabled=true,raw}={}){
  const captures=[],logs=[];
  const server=createServer(createHandler({config,now:()=>now,auth:{async verifyIdToken(){return identity;}},accounts:{async getOrCreate(){return {...account,access:enabled?'enabled':'disabled'};}},
    telemetry:configured?async(uid,events)=>{captures.push({uid,events});if(statusError)throw new Error('PRIVATE_TOKEN_AND_CONTENT');}:undefined,log:x=>logs.push(x)}));
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(()=>new Promise(resolve=>{server.close(resolve);server.closeAllConnections();}));
  const headers={'Content-Type':contentType};if(authorization!==null)headers.Authorization=authorization;
  const response=await fetch(`http://127.0.0.1:${server.address().port}/events`,{method:'POST',headers,body:raw??JSON.stringify(body)});
  return {status:response.status,body:await response.json(),captures,logs};
}
test('approved event families validate; preview, connection and output have separate meaning',()=>{
  const cases=[['session_started',{}],['video_state_changed',{preview_visible:false,consumer_connected:true,output_active:true,consumer_app:'brave'}],
    ['content_created',{kind:'annotation'}],['content_created',{kind:'ai_create',card_id:'saved_id'}],['content_created',{kind:'ai_revision',card_id:'saved_id'}],['content_created',{kind:'image_import',card_id:'saved_id'}],
    ['card_usage_changed',{card_id:'saved_id',card_type:'image',destination:'output',applied:false}]];
  for(const [event,properties] of cases)assert.equal(validateEvents({events:[{...sample,event,properties}]},now)[0].event,event);
  for(const properties of [{preview_visible:true,consumer_connected:false,output_active:true,consumer_app:'none'},
    {preview_visible:true,consumer_connected:true,output_active:false,consumer_app:'none'},
    {preview_visible:true,consumer_connected:false,output_active:false,consumer_app:'brave'}])
    assert.throws(()=>validateEvents({events:[{...sample,event:'video_state_changed',properties}]},now));
});
test('content, identity spoofing, unknown fields/events and malformed times are rejected as a whole',()=>{
  for(const patch of [{uid:'forged'},{properties:{prompt:'private'}},{event:'keystroke'},{appVersion:'name with spaces'},
    {id:'not-an-id'},{timestamp:'2026-02-30T00:00:00.000Z'},{timestamp:'2026-10-12T00:00:00.000Z'},{timestamp:'2026-10-14T03:00:00.000Z'}])
    assert.throws(()=>validateEvents({events:[{...sample,...patch}]},now));
  assert.throws(()=>validateEvents({events:[sample,sample]},now));assert.throws(()=>validateEvents({events:[],account:'forged'},now));
});
test('events require verified permitted account and do not consume AI allowance',async t=>{
  assert.equal((await request(t,{authorization:null})).status,401);
  assert.equal((await request(t,{enabled:false})).status,403);
  const result=await request(t);assert.equal(result.status,200);assert.deepEqual(result.body,{accepted:1});
  assert.deepEqual(result.captures,[{uid:identity.uid,events:[sample]}]);assert.equal(account.dailyGenerations,3);
});
test('unsupported media type, oversized body and private fields never reach the forwarder',async t=>{
  for(const [options,status] of [[{contentType:'text/plain'},415],[{raw:'x'.repeat(32769)},413],[{body:{events:[{...sample,properties:{token:'private'}}]}},400]]){
    const result=await request(t,options);assert.equal(result.status,status);assert.deepEqual(result.captures,[]);
  }
});
test('unconfigured and failed delivery return bounded retryable errors without private data',async t=>{
  assert.equal((await request(t,{configured:false})).status,503);
  const result=await request(t,{statusError:true});assert.equal(result.status,503);assert.deepEqual(result.body,{error:'telemetry_unavailable'});assert.deepEqual(result.logs,['telemetry_delivery_failed']);
});
test('forwarder preserves deduplication tuple and sends only approved data with server-derived owner',async()=>{
  const calls=[];const send=telemetryProvider({posthogHost:'https://us.i.posthog.com',posthogToken:'synthetic_project_token'},async(url,request)=>{calls.push({url,body:JSON.parse(request.body),request});return {ok:true};});
  await send('owner_A',[sample]);await send('owner_A',[sample]);await send('owner_B',[sample]);
  assert.deepEqual(calls[0].body,calls[1].body);assert.equal(calls[0].url,'https://us.i.posthog.com/batch/');
  const event=calls[0].body.batch[0];assert.equal(event.uuid,sample.id);assert.equal(event.timestamp,sample.timestamp);assert.equal(event.distinct_id,'callayers:owner_A');
  assert.equal(calls[2].body.batch[0].distinct_id,'callayers:owner_B');assert.equal(event.properties.$geoip_disable,true);assert.equal(event.properties.$process_person_profile,true);
  assert.equal(event.properties.source,'callayers_desktop');assert.equal(calls[0].request.redirect,'error');assert.ok(calls[0].request.signal);
  assert.deepEqual(Object.keys(event).sort(),['distinct_id','event','properties','timestamp','uuid']);
});
test('provider failure is generic and event limiter resets without persistent records',async()=>{
  const send=telemetryProvider({posthogHost:'https://us.i.posthog.com',posthogToken:'synthetic_project_token'},async()=>({ok:false}));
  await assert.rejects(()=>send('owner',[sample]),/telemetry_delivery_failed/);
  const permit=telemetryLimiter();assert.equal(permit('a',600,1),true);assert.equal(permit('a',1,2),false);assert.equal(permit('b',1,2),true);assert.equal(permit('a',1,60001),true);
});
test('telemetry config is explicit, region validated and token omitted from public configuration',()=>{
  const env={GOOGLE_CLOUD_PROJECT:'synthetic',FIRESTORE_DATABASE_ID:'callayers-v2',FIREBASE_WEB_API_KEY:'public',
    GOOGLE_DESKTOP_CLIENT_JSON:JSON.stringify({installed:{project_id:'synthetic',client_id:'synthetic.apps.googleusercontent.com',client_secret:'public'}})};
  assert.equal(loadConfig(env).posthogToken,'');
  assert.throws(()=>loadConfig({...env,POSTHOG_PROJECT_TOKEN:'synthetic_project_token'}));
  assert.throws(()=>loadConfig({...env,POSTHOG_PROJECT_TOKEN:'synthetic_project_token',POSTHOG_HOST:'http://private.invalid'}));
  const c=loadConfig({...env,POSTHOG_PROJECT_TOKEN:'synthetic_project_token',POSTHOG_HOST:'https://us.i.posthog.com/'});
  assert.equal(c.posthogHost,'https://us.i.posthog.com');assert.equal(c.publicConfig.posthogToken,undefined);assert.equal(c.publicConfig.posthogHost,undefined);
});
