import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { runInNewContext } from 'node:vm';
const script = await readFile(new URL('../website/public/analytics.js', import.meta.url), 'utf8');

async function visit({ signedIn = false, previous, blocked = false, invalidTicket = false, htmlPath = false } = {}) {
 const events=[], calls=[], requests=[], listeners={};let options;
 const location={pathname:signedIn?(htmlPath?'/signed-in.html':'/signed-in'):'/',hash:signedIn?'#ticket='+'a'.repeat(43):'',
  href:signedIn?'https://callayers.com/signed-in.html#ticket='+'a'.repeat(43):'https://callayers.com/?utm_source=test&token=PRIVATE'};
 const sdk={capture(event,properties={}){events.push(options.before_send({event,properties}));},
  register(){},identify(id){calls.push(['identify',id]);},get_property(){return previous;},reset(){calls.push(['reset']);}};
 const context={location,URL,URLSearchParams,AbortSignal,setTimeout,clearTimeout,window:{},
  history:{replaceState(_,__,path){location.href='https://callayers.com'+path;location.hash='';calls.push(['clean_url']);}},
  document:{addEventListener(name,handler){listeners[name]=handler;},createElement(){return {};},
   getElementsByTagName(){return [{parentNode:{insertBefore(){queueMicrotask(()=>{
    options=context.window.posthog._i[0][1];calls.push(['sdk_loaded']);options.loaded(sdk);
   });}}}];}},
  fetch:async(url,init={})=>{
   requests.push({url,init});
   if(url.endsWith('/web-analytics-config')){
    if(blocked)throw Error('blocked');
    return Response.json({token:'synthetic_public_capture_key',host:'https://us.i.posthog.com'});
   }
   return invalidTicket?new Response(null,{status:410}):Response.json({distinctId:'callayers:owner'});
  },
 };
 runInNewContext(script,context);
 await new Promise(resolve=>setTimeout(resolve,10));
 return {events,calls,requests,listeners,options,context};
}

test('campaign pageviews and explicit download events work without recording or private query parameters', async()=>{
 const page=await visit();
 assert.equal(page.events[0].event,'$pageview');
 assert.equal(page.events[0].properties.$current_url,'https://callayers.com/?utm_source=test');
 assert.equal(page.options.disable_session_recording,true);assert.equal(page.options.autocapture,false);
 page.listeners.click({target:{closest:()=>({id:'installer-link',dataset:{version:'0.3.0'}})}});
 assert.equal(page.events[1].event,'installer_download_clicked');assert.equal(page.events[1].properties.version,'0.3.0');
 page.listeners.click({target:{closest:()=>({id:'',dataset:{version:'0.3.9',downloadPlatform:'windows'},closest:()=>null})}});
 assert.equal(page.events[2].event,'installer_download_clicked');assert.equal(page.events[2].properties.button_location,'hero');
 const filtered=page.options.before_send({properties:{$referrer:'https://example.com/?code=PRIVATE#secret',
  $set_once:{$initial_current_url:'https://callayers.com/?secret=PRIVATE&utm_source=test#ticket=PRIVATE'}}});
 assert.equal(filtered.properties.$referrer,'https://example.com/');
 assert.equal(filtered.properties.$set_once.$initial_current_url,'https://callayers.com/?utm_source=test');
});

test('verified ticket identifies original anonymous browser, removes ticket before SDK, and keeps auth credentials out of requests', async()=>{
 const page=await visit({signedIn:true});
 assert.deepEqual(page.calls,[['clean_url'],['sdk_loaded'],['identify','callayers:owner']]);
 assert.deepEqual(page.events.map(x=>x.event),['desktop_account_connected']);
 assert.equal(page.context.location.hash,'');
 const consume=page.requests[1];assert.deepEqual(JSON.parse(consume.init.body),{ticket:'a'.repeat(43)});
 assert.equal(consume.init.headers.Authorization,undefined);
 const shared=await visit({signedIn:true,previous:'callayers:someone_else'});
 assert.deepEqual(shared.calls.slice(-2),[['reset'],['identify','callayers:owner']]);
 const html=await visit({signedIn:true,htmlPath:true});
 assert.deepEqual(html.calls.slice(-1),[['identify','callayers:owner']]);
});

test('setup booking clicks are distinct from downloads and do not block navigation',async()=>{
 const page=await visit();
 for(const button_location of ['navigation','hero','setup']){
  page.listeners.click({target:{closest:()=>({dataset:{bookingLocation:button_location}})},
   preventDefault(){throw Error('Booking navigation must not be blocked');}});
  const event=page.events.at(-1);
  assert.equal(event.event,'setup_booking_clicked');
  assert.equal(event.properties.button_location,button_location);
  assert.equal(event.properties.source,'callayers_website');
 }
 const blocked=await visit({blocked:true});
 assert.doesNotThrow(()=>blocked.listeners.click({target:{closest:()=>({dataset:{bookingLocation:'hero'}})}}));
});

test('blocked analytics or an expired/replayed ticket never identifies an account or changes the completion page', async()=>{
 const blocked=await visit({signedIn:true,blocked:true});
 assert.deepEqual(blocked.calls,[['clean_url']]);assert.equal(blocked.requests.length,1);
 const expired=await visit({signedIn:true,invalidTicket:true});
 assert.deepEqual(expired.calls,[['clean_url'],['sdk_loaded']]);assert.deepEqual(expired.events,[]);
});
