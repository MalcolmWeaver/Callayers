import { readGeneration, validateCard } from './generation.js';
import { readEvents, telemetryLimiter } from './telemetry.js';
import { WEBSITE_ORIGIN, newTicket, readTicket } from './attribution.js';
const invalidCredentialCodes = new Set([
  'auth/argument-error', 'auth/invalid-argument', 'auth/invalid-id-token',
  'auth/id-token-expired', 'auth/id-token-revoked', 'auth/user-not-found',
]);
function reply(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
  res.end(JSON.stringify(body));
}
export function createHandler({ config, auth, accounts, generate, telemetry, now = () => new Date(), log = () => {} }) {
  const permitEvents=telemetryLimiter();
  return async (req, res) => {
      // No raw request-body/token/profile logging. Browser access is limited to analytics routes.
    try {
      const webAnalytics = req.url === '/web-analytics-config' || req.url === '/attribution/consume';
      if (webAnalytics) {
        if (req.headers.origin !== WEBSITE_ORIGIN) return reply(res, 403, { error: 'origin_not_allowed' });
        res.setHeader('Access-Control-Allow-Origin', WEBSITE_ORIGIN);
        res.setHeader('Vary', 'Origin');
        if (req.method === 'OPTIONS') {
          res.setHeader('Access-Control-Allow-Methods', req.url === '/web-analytics-config' ? 'GET' : 'POST');
          res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
          res.writeHead(204); return res.end();
        }
        if (req.url === '/web-analytics-config' && req.method === 'GET') {
          // Project capture keys are public SDK configuration, never personal API keys.
          return reply(res, 200, { token: config.posthogToken || '', host: config.posthogHost || '' });
        }
        if (req.url === '/attribution/consume' && req.method === 'POST') {
          const ticket = await readTicket(req);
          if (!ticket) return reply(res, 400, { error: 'invalid_ticket' });
          const uid = await accounts.consumeAttributionTicket(ticket, now());
          if (!uid) return reply(res, 410, { error: 'ticket_unavailable' });
          return reply(res, 200, { distinctId: `callayers:${uid}` });
        }
        return reply(res, 405, { error: 'method_not_allowed' });
      }
      const attribution = req.url === '/attribution/ticket' && req.method === 'POST';
      const generation=req.url==='/generate' && req.method==='POST' && typeof generate==='function';
      const events=req.url==='/events' && req.method==='POST';
      if (req.method !== 'GET' && !generation && !events && !attribution) return reply(res, 405, { error: 'method_not_allowed' });
      if (req.url === '/health') return reply(res, 200, { service: 'callayers-v2-api', version: 2, status: 'ok', generationReady:typeof generate==='function',revisionReady:typeof generate==='function',telemetryReady:typeof telemetry==='function' });
      if (req.url === '/client-config') return reply(res, 200, config.publicConfig);
      if (req.url !== '/account' && !generation && !events && !attribution) return reply(res, 404, { error: 'not_found' });
      const authorization = req.headers.authorization;
      if (typeof authorization !== 'string' || !/^Bearer [A-Za-z0-9_.-]{1,16384}$/.test(authorization))
        return reply(res, 401, { error: 'sign_in_required' });
      let identity;
      try { identity = await auth.verifyIdToken(authorization.slice(7), true); }
      catch (error) {
        if (error.code === 'auth/user-disabled') return reply(res, 403, { error: 'account_unavailable' });
        if (invalidCredentialCodes.has(error.code)) return reply(res, 401, { error: 'sign_in_required' });
        throw error;
      }
      if (identity.aud !== config.projectId || identity.iss !== `https://securetoken.google.com/${config.projectId}` ||
          typeof identity.uid !== 'string' || identity.uid.length < 1 || identity.uid.length > 128 ||
          /[\/\x00-\x1f\x7f]/.test(identity.uid) || identity.uid === '.' || identity.uid === '..')
        return reply(res, 401, { error: 'sign_in_required' });
      if (identity.firebase?.sign_in_provider !== 'google.com' || identity.email_verified !== true)
        return reply(res, 403, { error: 'google_sign_in_required' });
      const today = now().toISOString().slice(0, 10);
      const account = await accounts.getOrCreate(identity.uid, today);
      if (account.schemaVersion !== 2 || account.access !== 'enabled')
        return reply(res, 403, { error: 'account_unavailable' });
      if (attribution) {
        if (!config.posthogToken) return reply(res, 503, { error: 'analytics_unavailable' });
        if (!permitEvents(identity.uid, 1, now().getTime())) return reply(res, 429, { error: 'rate_limited' });
        const ticket = newTicket();
        await accounts.createAttributionTicket(identity.uid, ticket, new Date(now().getTime() + 120000));
        return reply(res, 200, { ticket });
      }
      if(events){
        let batch;try{batch=await readEvents(req,now());}catch(error){return reply(res,error.status||400,{error:'invalid_events'});}
        if(typeof telemetry!=='function')return reply(res,503,{error:'telemetry_unavailable'});
        if(!permitEvents(identity.uid,batch.length,now().getTime()))return reply(res,429,{error:'telemetry_rate_limited'});
        try{await telemetry(identity.uid,batch);return reply(res,200,{accepted:batch.length});}
        catch{log('telemetry_delivery_failed');return reply(res,503,{error:'telemetry_unavailable'});}
      }
      if (!Number.isSafeInteger(account.dailyGenerations) || account.dailyGenerations < 0 ||
          typeof account.usageDate !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(account.usageDate))
        throw new Error('Invalid account data');
      const used=account.usageDate===today?account.dailyGenerations:0,limit=config.dailyLimit;
      const allowance=count=>({date:today,used:count,enabled:typeof generate==='function',limit:typeof generate==='function'?limit:null,remaining:typeof generate==='function'?Math.max(0,limit-count):null});
      if(generation){
        let input;try{input=await readGeneration(req);}catch(error){return reply(res,error.status||400,{error:error.code||'invalid_request'});}
        const reserved=await accounts.reserve(identity.uid,input.requestId,today,limit,new Date(now().getTime()+86400000));
        if(reserved.kind==='duplicate')return reply(res,409,{error:'request_already_used',requestStatus:reserved.status});
        if(reserved.kind==='wrong_owner')return reply(res,404,{error:'request_not_found'});
        if(reserved.kind==='denied')return reply(res,403,{error:'account_unavailable'});
        if(reserved.kind==='limit')return reply(res,429,{error:'daily_limit_reached',allowance:allowance(reserved.used)});
        try{
          const card=validateCard(await generate(input.prompt,input.currentCard,input.attachments));
          if(input.currentCard?.imageIds.length && !card.html.includes('callayers-image:local'))throw new Error('missing_image_reference');
          await accounts.finish(identity.uid,input.requestId,'completed');
          return reply(res,200,{requestId:input.requestId,card,allowance:allowance(reserved.used)});
        }catch{
          try{await accounts.finish(identity.uid,input.requestId,'failed');}catch{}
          log('generation_failed');return reply(res,502,{error:'generation_failed',allowance:allowance(reserved.used)});
        }
      }
      return reply(res, 200, { uid: identity.uid,
        email: typeof identity.email === 'string' ? identity.email : '', accessAllowed: true,
        allowance: allowance(used) });
    } catch {
      // Operational event only. SDK exceptions may contain private request data.
      log('account_service_unavailable');
      if (!res.headersSent) reply(res, 503, { error: 'temporarily_unavailable' });
      else res.destroy();
    }
  };
}
