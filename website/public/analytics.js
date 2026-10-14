(() => {
 const service='https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app';
 const signedIn=['/signed-in','/signed-in.html'].includes(location.pathname);
 const ticket=signedIn?new URLSearchParams(location.hash.slice(1)).get('ticket'):null;
 // Remove the one-use code before the SDK loads or observes the URL.
 if(signedIn)history.replaceState(null,'',location.pathname);
 const safeUrl=value=>{
  try{
   const url=new URL(value);url.hash='';
   const allowed=new URLSearchParams();
   for(const key of ['utm_source','utm_medium','utm_campaign','utm_term','utm_content']){
    if(url.searchParams.has(key))allowed.set(key,url.searchParams.get(key).slice(0,200));
   }
   url.search=allowed.toString();return url.href;
  }catch{return '';}
 };
 let sdk=null;
 const pending=[];let unavailable=false;
 window.callayersTrack=(event,properties={})=>{
  try{
   if(sdk)sdk.capture(event,{...properties,source:'callayers_website'});
   else if(!unavailable&&pending.length<32)pending.push([event,properties]);
  }catch{ /* Analytics must not interfere with website controls. */ }
 };
 const ready=new Promise(resolve=>{
  const timer=setTimeout(()=>resolve(null),5000);
  (async()=>{
   try{
    const response=await fetch(`${service}/web-analytics-config`,{signal:AbortSignal.timeout(3000),credentials:'omit'});
    if(!response.ok)throw Error();
    const config=await response.json();
    if(!/^[A-Za-z0-9_-]{16,200}$/.test(config.token)||!['https://us.i.posthog.com','https://eu.i.posthog.com'].includes(config.host))throw Error();
    // PostHog's HTML loader, kept in a local file so our CSP needs no inline script.
    !function(t,e){var o,n,p,r;e.__SV||(window.posthog=e,e._i=[],e.init=function(i,s,a){function g(t,e){var o=e.split('.');2==o.length&&(t=t[o[0]],e=o[1]),t[e]=function(){t.push([e].concat(Array.prototype.slice.call(arguments,0)))}}(p=t.createElement('script')).type='text/javascript',p.crossOrigin='anonymous',p.async=!0,p.src=s.api_host.replace('.i.posthog.com','-assets.i.posthog.com')+'/static/array.js',p.onerror=function(){clearTimeout(timer);resolve(null)},(r=t.getElementsByTagName('script')[0]).parentNode.insertBefore(p,r);var u=e;for(void 0!==a?u=e[a]=[]:a='posthog',u.people=u.people||[],Object.defineProperty(u,'toString',{configurable:!0,enumerable:!0,writable:!0,value:function(t){var e='posthog';return'posthog'!==a&&(e+='.'+a),t||(e+=' (stub)'),e}}),Object.defineProperty(u.people,'toString',{configurable:!0,enumerable:!0,writable:!0,value:function(){return u.toString(1)+'.people (stub)'}}),o='init capture register register_once register_for_session unregister unregister_for_session getFeatureFlag getFeatureFlagResult isFeatureEnabled reloadFeatureFlags updateEarlyAccessFeatureEnrollment getEarlyAccessFeatures on onFeatureFlags onSessionId getSurveys getActiveMatchingSurveys renderSurvey canRenderSurvey getNextSurveyStep identify setPersonProperties group resetGroups setPersonPropertiesForFlags resetPersonPropertiesForFlags setGroupPropertiesForFlags resetGroupPropertiesForFlags resetGroups reset get_distinct_id getGroups get_session_id get_session_replay_url alias set_config startSessionRecording stopSessionRecording sessionRecordingStarted captureException loadToolbar get_property getSessionProperty createPersonProfile opt_in_capturing opt_out_capturing has_opted_in_capturing has_opted_out_capturing clear_opt_in_out_capturing debug'.split(' '),n=0;n<o.length;n++)g(u,o[n]);e._i.push([i,s,a])},e.__SV=1)}(document,window.posthog||[]);
    const ph=window.posthog;
    ph.init(config.token,{
     api_host:config.host,person_profiles:'identified_only',persistence:'localStorage+cookie',
     autocapture:false,capture_pageview:false,capture_pageleave:false,
     disable_session_recording:true,advanced_disable_flags:true,
     capture_exceptions:false,capture_heatmaps:false,
     before_send:event=>{
      if(event?.properties){
       for(const properties of [event.properties,event.properties.$set,event.properties.$set_once]){
        if(!properties)continue;
        for(const key of ['$current_url','$referrer','$initial_current_url','$initial_referrer','$session_entry_url','$session_exit_url'])
         if(properties[key])properties[key]=safeUrl(properties[key]);
       }
      }
      return event;
     },
     loaded:instance=>{clearTimeout(timer);resolve(instance);},
    });
   }catch{clearTimeout(timer);resolve(null);}
  })();
 });
 ready.then(async instance=>{
  if(!instance){unavailable=true;pending.length=0;return;}
  sdk=instance;
  sdk.register({source:'callayers_website'});
  if(signedIn){
   if(!ticket||!/^[A-Za-z0-9_-]{43}$/.test(ticket))return;
   try{
    const response=await fetch(`${service}/attribution/consume`,{
     method:'POST',headers:{'Content-Type':'application/json'},credentials:'omit',
     body:JSON.stringify({ticket}),signal:AbortSignal.timeout(4000),
    });
    if(!response.ok)return;
    const {distinctId}=await response.json();
    if(typeof distinctId!=='string'||!/^callayers:[^\/\x00-\x1f\x7f]{1,128}$/.test(distinctId))return;
    // A shared browser may have belonged to a different signed-in account.
    const previous=sdk.get_property('$user_id');
    if(previous&&previous!==distinctId){sdk.reset();sdk.register({source:'callayers_website'});}
    sdk.identify(distinctId);
    sdk.capture('desktop_account_connected');
   }catch{ /* Sign-in has already succeeded in the desktop. */ }
  }else{
   window.callayersTrack('$pageview',{$current_url:safeUrl(location.href)});
   for(const [event,properties]of pending.splice(0))window.callayersTrack(event,properties);
  }
 });
 document.addEventListener('click',event=>{
  const link=event.target.closest?.('a');if(!link)return;
  if(link.id==='installer-link')window.callayersTrack('installer_download_clicked',{version:link.dataset.version,button_location:'download'});
  else if(link.getAttribute('href')==='#download')window.callayersTrack('get_callayers_clicked',{button_location:link.closest('header')?'navigation':'hero'});
 });
})();
